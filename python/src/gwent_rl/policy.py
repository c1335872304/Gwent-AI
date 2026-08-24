from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Mapping

import numpy as np
import torch
from torch import nn

from .schema import (
    GLOBAL_FEATURE_NAMES,
    OBJECT_FEATURE_NAMES,
    OPTION_FEATURE_NAMES,
)
from .text_embeddings import TextEmbeddingTable


@dataclass(frozen=True)
class PolicyOutput:
    logits: torch.Tensor
    values: torch.Tensor


def masked_logits(logits: torch.Tensor, option_mask: torch.Tensor) -> torch.Tensor:
    """Set illegal option logits to a very negative finite value.

    If a malformed row has no legal action, row 0 is made temporarily legal so
    PyTorch's Categorical distribution does not produce NaNs.  The C collector
    should never emit such a row; this is a defensive guard for training.
    """
    mask = option_mask.to(dtype=torch.bool, device=logits.device)
    if mask.ndim != 2:
        raise ValueError("option_mask must be [B,K]")
    no_legal = ~mask.any(dim=1)
    if no_legal.any():
        mask = mask.clone()
        mask[no_legal, 0] = True
    neg = torch.finfo(logits.dtype).min / 4
    return logits.masked_fill(~mask, neg)


def sample_actions(logits: torch.Tensor, option_mask: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    """Sample legal option indices and return (actions, log_probs, entropy)."""
    masked = masked_logits(logits, option_mask)
    dist = torch.distributions.Categorical(logits=masked)
    actions = dist.sample()
    return actions, dist.log_prob(actions), dist.entropy()


def greedy_actions(logits: torch.Tensor, option_mask: torch.Tensor) -> torch.Tensor:
    return torch.argmax(masked_logits(logits, option_mask), dim=-1)


def _ids(batch: Mapping[str, torch.Tensor], key: str, shape: tuple[int, ...], device: torch.device) -> torch.Tensor:
    value = batch.get(key)
    if value is None:
        return torch.full(shape, -1, dtype=torch.long, device=device)
    return value.to(device=device, dtype=torch.long)


def _bucket_ids(ids: torch.Tensor, size: int) -> torch.Tensor:
    clamped = torch.clamp(ids, min=0)
    return torch.remainder(clamped, int(size)).long()


def _gather_tokens(tokens: torch.Tensor, indices: torch.Tensor) -> torch.Tensor:
    """Gather [B,N,H] tokens by [B] or [B,K] indices; invalid indices -> zero."""
    bsz, count, hidden = tokens.shape
    if indices.dim() == 1:
        valid = (indices >= 0) & (indices < count)
        safe = indices.clamp(min=0, max=max(count - 1, 0)).view(bsz, 1, 1).expand(bsz, 1, hidden)
        gathered = tokens.gather(1, safe).squeeze(1)
        return gathered * valid.to(tokens.dtype).unsqueeze(-1)
    if indices.dim() == 2:
        k = indices.shape[1]
        valid = (indices >= 0) & (indices < count)
        safe = indices.clamp(min=0, max=max(count - 1, 0)).unsqueeze(-1).expand(bsz, k, hidden)
        gathered = tokens.gather(1, safe)
        return gathered * valid.to(tokens.dtype).unsqueeze(-1)
    raise ValueError(f"unsupported index rank: {indices.dim()}")


def _masked_mean(tokens: torch.Tensor, mask: torch.Tensor) -> torch.Tensor:
    weights = mask.to(tokens.dtype).unsqueeze(-1)
    denom = weights.sum(dim=1).clamp_min(1.0)
    return (tokens * weights).sum(dim=1) / denom


def _safe_key_padding_mask(mask: torch.Tensor) -> torch.Tensor:
    """MultiheadAttention cannot handle rows with every key masked."""
    key_padding = ~mask.to(dtype=torch.bool)
    if key_padding.ndim != 2:
        raise ValueError("expected [B,N] mask")
    all_masked = key_padding.all(dim=1)
    if all_masked.any():
        key_padding = key_padding.clone()
        key_padding[all_masked, 0] = False
    return key_padding


class CandidatePolicyValueNet(nn.Module):
    """Candidate policy/value net optimized for structured sequential-decision batches.

    m59 keeps the C ABI stable and improves the Python model path:

    * optional offline card text embeddings are fused into object and option
      card tokens;
    * option tokens use gated source/target fusion, so targeted choices can
      emphasize the referenced objects;
    * stacked candidate-object cross attention and candidate self attention are
      configurable;
    * all mask handling is NaN-safe for long PPO runs.
    """

    def __init__(
        self,
        global_dim: int = len(GLOBAL_FEATURE_NAMES),
        object_dim: int = len(OBJECT_FEATURE_NAMES),
        option_dim: int = len(OPTION_FEATURE_NAMES),
        hidden_dim: int = 128,
        num_attention_heads: int = 4,
        attention_layers: int = 1,
        dropout: float = 0.0,
        card_vocab_size: int = 4096,
        max_prefix: int = 16,
        max_objects: int = 128,
        max_options: int = 256,
        text_embedding_dim: int = 0,
        freeze_text_embeddings: bool = True,
        use_candidate_object_attention: bool = True,
        use_candidate_self_attention: bool = True,
    ):
        super().__init__()
        self.global_dim = int(global_dim)
        self.object_dim = int(object_dim)
        self.option_dim = int(option_dim)
        self.hidden_dim = int(hidden_dim)
        self.num_attention_heads = int(num_attention_heads)
        self.attention_layers = int(attention_layers)
        self.dropout = float(dropout)
        self.card_vocab_size = int(card_vocab_size)
        self.max_prefix = int(max_prefix)
        self.max_objects = int(max_objects)
        self.max_options = int(max_options)
        self.text_embedding_dim = int(text_embedding_dim)
        self.freeze_text_embeddings = bool(freeze_text_embeddings)
        self.use_candidate_object_attention = bool(use_candidate_object_attention)
        self.use_candidate_self_attention = bool(use_candidate_self_attention)

        if self.hidden_dim % self.num_attention_heads != 0:
            raise ValueError("hidden_dim must be divisible by num_attention_heads")

        self.global_encoder = nn.Sequential(
            nn.Linear(self.global_dim, hidden_dim),
            nn.LayerNorm(hidden_dim),
            nn.GELU(),
            nn.Linear(hidden_dim, hidden_dim),
            nn.GELU(),
        )
        self.object_feature_encoder = nn.Sequential(
            nn.Linear(self.object_dim, hidden_dim),
            nn.LayerNorm(hidden_dim),
            nn.GELU(),
        )
        self.option_feature_encoder = nn.Sequential(
            nn.Linear(self.option_dim, hidden_dim),
            nn.LayerNorm(hidden_dim),
            nn.GELU(),
        )

        self.card_embedding = nn.Embedding(self.card_vocab_size, hidden_dim)
        self.zone_embedding = nn.Embedding(32, hidden_dim)
        self.row_embedding = nn.Embedding(32, hidden_dim)
        self.owner_embedding = nn.Embedding(8, hidden_dim)
        self.slot_embedding = nn.Embedding(512, hidden_dim)
        self.insert_position_embedding = nn.Embedding(32, hidden_dim)
        self.decision_embedding = nn.Embedding(32, hidden_dim)
        self.option_kind_embedding = nn.Embedding(64, hidden_dim)
        self.prefix_kind_embedding = nn.Embedding(64, hidden_dim)
        self.prefix_position_embedding = nn.Embedding(self.max_prefix, hidden_dim)
        self.prefix_gru = nn.GRU(hidden_dim, hidden_dim, batch_first=True)
        self.source_missing = nn.Parameter(torch.zeros(hidden_dim))

        self.object_norm = nn.LayerNorm(hidden_dim)
        self.option_norm = nn.LayerNorm(hidden_dim)
        self.state_norm = nn.LayerNorm(hidden_dim)
        self.prefix_norm = nn.LayerNorm(hidden_dim)
        self.relation_norm = nn.LayerNorm(hidden_dim)

        self.source_target_gate = nn.Sequential(
            nn.Linear(hidden_dim * 3, hidden_dim),
            nn.Sigmoid(),
        )
        self.target_gate = nn.Sequential(
            nn.Linear(hidden_dim * 3, hidden_dim),
            nn.Sigmoid(),
        )

        self.candidate_object_attention = nn.ModuleList(
            nn.MultiheadAttention(hidden_dim, self.num_attention_heads, dropout=self.dropout, batch_first=True)
            for _ in range(self.attention_layers)
        )
        self.candidate_self_attention = nn.ModuleList(
            nn.MultiheadAttention(hidden_dim, self.num_attention_heads, dropout=self.dropout, batch_first=True)
            for _ in range(self.attention_layers)
        )
        self.candidate_ffn = nn.ModuleList(
            nn.Sequential(
                nn.LayerNorm(hidden_dim),
                nn.Linear(hidden_dim, hidden_dim * 2),
                nn.GELU(),
                nn.Dropout(self.dropout),
                nn.Linear(hidden_dim * 2, hidden_dim),
            )
            for _ in range(self.attention_layers)
        )

        self.logit_head = nn.Sequential(
            nn.LayerNorm(hidden_dim * 5),
            nn.Linear(hidden_dim * 5, hidden_dim),
            nn.GELU(),
            nn.Dropout(self.dropout),
            nn.Linear(hidden_dim, 1),
        )
        self.value_head = nn.Sequential(
            nn.LayerNorm(hidden_dim * 4),
            nn.Linear(hidden_dim * 4, hidden_dim),
            nn.GELU(),
            nn.Dropout(self.dropout),
            nn.Linear(hidden_dim, 1),
        )

        self._init_text_embedding_modules(num_rows=1, dim=self.text_embedding_dim, lookup_size=1)

    def _init_text_embedding_modules(self, num_rows: int, dim: int, lookup_size: int) -> None:
        dim = int(dim)
        self.text_embedding_dim = dim
        if dim > 0:
            self.text_embedding = nn.Embedding(max(int(num_rows), 1), dim)
            self.text_embedding.weight.requires_grad_(not self.freeze_text_embeddings)
            self.text_projection = nn.Linear(dim, self.hidden_dim, bias=False)
        else:
            self.text_embedding = None
            self.text_projection = None
        lookup = torch.zeros((max(int(lookup_size), 1),), dtype=torch.long)
        if "text_card_id_lookup" in self._buffers:
            self._buffers["text_card_id_lookup"] = lookup
        else:
            self.register_buffer("text_card_id_lookup", lookup, persistent=True)

    def set_text_embeddings(
        self,
        card_ids: np.ndarray | torch.Tensor,
        embeddings: np.ndarray | torch.Tensor,
        *,
        freeze: bool | None = None,
    ) -> None:
        emb = torch.as_tensor(embeddings, dtype=torch.float32)
        ids = torch.as_tensor(card_ids, dtype=torch.long)
        if emb.ndim != 2:
            raise ValueError("embeddings must be [rows, dim]")
        if ids.ndim != 1 or ids.shape[0] != emb.shape[0]:
            raise ValueError("card_ids must be [rows] with same row count as embeddings")
        if freeze is not None:
            self.freeze_text_embeddings = bool(freeze)
        max_id = int(ids.clamp_min(0).max().item()) if ids.numel() else 0
        self._init_text_embedding_modules(num_rows=emb.shape[0], dim=emb.shape[1], lookup_size=max_id + 1)
        assert self.text_embedding is not None
        with torch.no_grad():
            self.text_embedding.weight.copy_(emb)
            lookup = torch.zeros((max_id + 1,), dtype=torch.long)
            for row, raw_id in enumerate(ids.tolist()):
                if int(raw_id) >= 0:
                    lookup[int(raw_id)] = int(row)
            self.text_card_id_lookup = lookup
        self.text_embedding.weight.requires_grad_(not self.freeze_text_embeddings)

    def set_text_embedding_table(self, table: TextEmbeddingTable, *, freeze: bool | None = None) -> None:
        t = table.validate()
        self.set_text_embeddings(t.card_ids, t.embeddings, freeze=freeze)

    def _text_tokens(self, card_ids: torch.Tensor) -> torch.Tensor:
        if self.text_embedding is None or self.text_projection is None or self.text_embedding_dim <= 0:
            return torch.zeros((*card_ids.shape, self.hidden_dim), dtype=torch.float32, device=card_ids.device)
        lookup = self.text_card_id_lookup.to(device=card_ids.device)
        valid = (card_ids >= 0) & (card_ids < lookup.shape[0])
        safe = card_ids.clamp(min=0, max=max(int(lookup.shape[0]) - 1, 0))
        rows = lookup[safe] * valid.to(dtype=torch.long)
        text = self.text_embedding(rows)
        return self.text_projection(text)

    def model_manifest(self) -> dict[str, object]:
        return {
            "architecture": "single_head",
            "hidden_dim": self.hidden_dim,
            "num_attention_heads": self.num_attention_heads,
            "attention_layers": self.attention_layers,
            "dropout": self.dropout,
            "card_vocab_size": self.card_vocab_size,
            "text_embedding_dim": self.text_embedding_dim,
            "freeze_text_embeddings": self.freeze_text_embeddings,
            "use_candidate_object_attention": self.use_candidate_object_attention,
            "use_candidate_self_attention": self.use_candidate_self_attention,
            "prefix_encoder": "position_gru_v1",
        }

    def _object_tokens(self, batch: Mapping[str, torch.Tensor], device: torch.device) -> tuple[torch.Tensor, torch.Tensor]:
        object_features = batch["object_features"].to(device=device, dtype=torch.float32)
        object_mask = batch["object_mask"].to(device=device, dtype=torch.bool)
        bsz, n, _ = object_features.shape
        card_ids = _ids(batch, "object_card_ids", (bsz, n), device)
        zone_ids = _ids(batch, "object_zone_ids", (bsz, n), device)
        row_ids = _ids(batch, "object_row_ids", (bsz, n), device)
        owner_ids = _ids(batch, "object_owner_ids", (bsz, n), device)
        controller_ids = _ids(batch, "object_controller_ids", (bsz, n), device)
        slot_ids = _ids(batch, "object_slot_indices", (bsz, n), device)

        tokens = self.object_feature_encoder(object_features)
        tokens = tokens + self.card_embedding(_bucket_ids(card_ids, self.card_vocab_size))
        tokens = tokens + self._text_tokens(card_ids)
        tokens = tokens + self.zone_embedding(_bucket_ids(zone_ids, 32))
        tokens = tokens + self.row_embedding(_bucket_ids(row_ids, 32))
        tokens = tokens + self.owner_embedding(_bucket_ids(owner_ids, 8))
        tokens = tokens + self.owner_embedding(_bucket_ids(controller_ids, 8))
        tokens = tokens + self.slot_embedding(_bucket_ids(slot_ids, 512))
        tokens = self.object_norm(tokens)
        return tokens * object_mask.to(tokens.dtype).unsqueeze(-1), object_mask

    def _prefix_context(self, batch: Mapping[str, torch.Tensor], object_tokens: torch.Tensor, device: torch.device) -> torch.Tensor:
        bsz = object_tokens.shape[0]
        prefix_mask = batch.get("prefix_mask")
        if prefix_mask is None:
            return torch.zeros((bsz, self.hidden_dim), dtype=object_tokens.dtype, device=device)
        prefix_mask = prefix_mask.to(device=device, dtype=torch.bool)
        p = prefix_mask.shape[1]
        prefix_kind_ids = _ids(batch, "prefix_kind_ids", (bsz, p), device)
        prefix_row_ids = _ids(batch, "prefix_row_ids", (bsz, p), device)
        prefix_insert_positions = _ids(batch, "prefix_insert_positions", (bsz, p), device)
        prefix_source = _ids(batch, "prefix_source_object_indices", (bsz, p), device)
        prefix_target = _ids(batch, "prefix_target_object_indices", (bsz, p), device)
        prefix_card_ids = _ids(batch, "prefix_card_ids", (bsz, p), device)
        source_tokens = _gather_tokens(object_tokens, prefix_source)
        target_tokens = _gather_tokens(object_tokens, prefix_target)
        tokens = self.prefix_kind_embedding(_bucket_ids(prefix_kind_ids, 64))
        tokens = tokens + self.row_embedding(_bucket_ids(prefix_row_ids, 32))
        tokens = tokens + self.insert_position_embedding(_bucket_ids(prefix_insert_positions, 32))
        prefix_card_tokens = self.card_embedding(_bucket_ids(prefix_card_ids, self.card_vocab_size))
        prefix_card_tokens = prefix_card_tokens + self._text_tokens(prefix_card_ids)
        prefix_card_tokens = prefix_card_tokens * (prefix_card_ids >= 0).to(tokens.dtype).unsqueeze(-1)
        tokens = tokens + prefix_card_tokens
        tokens = tokens + source_tokens + target_tokens
        positions = torch.arange(p, device=device).view(1, p).expand(bsz, p)
        positions = positions.clamp(max=max(self.max_prefix - 1, 0))
        tokens = tokens + self.prefix_position_embedding(positions)
        tokens = self.prefix_norm(tokens)
        tokens = tokens * prefix_mask.to(tokens.dtype).unsqueeze(-1)

        encoded, _ = self.prefix_gru(tokens)
        lengths = prefix_mask.sum(dim=1)
        last_index = (lengths - 1).clamp_min(0)
        context = encoded[torch.arange(bsz, device=device), last_index]
        return context * (lengths > 0).to(context.dtype).unsqueeze(-1)

    def _state_token(self, batch: Mapping[str, torch.Tensor], object_tokens: torch.Tensor, object_mask: torch.Tensor, device: torch.device) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        global_features = batch["global_features"].to(device=device, dtype=torch.float32)
        state_token = self.global_encoder(global_features)
        decision_kinds = _ids(batch, "decision_kinds", (global_features.shape[0],), device)
        state_token = state_token + self.decision_embedding(_bucket_ids(decision_kinds, 32))

        pooled_objects = _masked_mean(object_tokens, object_mask)
        source_indices = _ids(batch, "source_object_indices", (global_features.shape[0],), device)
        source_token = _gather_tokens(object_tokens, source_indices)
        missing = (source_indices < 0).to(dtype=source_token.dtype).unsqueeze(-1)
        source_token = source_token + missing * self.source_missing.view(1, -1)
        prefix_context = self._prefix_context(batch, object_tokens, device)
        state_token = self.state_norm(state_token + source_token + prefix_context)
        return state_token, pooled_objects, source_token

    def _option_tokens(self, batch: Mapping[str, torch.Tensor], object_tokens: torch.Tensor, state_token: torch.Tensor, device: torch.device) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
        option_features = batch["option_features"].to(device=device, dtype=torch.float32)
        option_mask = batch["option_mask"].to(device=device, dtype=torch.bool)
        bsz, k, _ = option_features.shape
        kind_ids = _ids(batch, "option_kind_ids", (bsz, k), device)
        card_ids = _ids(batch, "option_card_ids", (bsz, k), device)
        row_ids = _ids(batch, "option_target_row_ids", (bsz, k), device)
        insert_positions = _ids(batch, "option_insert_positions", (bsz, k), device)
        hand_slots = _ids(batch, "option_hand_slot_indices", (bsz, k), device)
        source_indices = _ids(batch, "option_source_object_indices", (bsz, k), device)
        target_indices = _ids(batch, "option_target_object_indices", (bsz, k), device)

        source_tokens = _gather_tokens(object_tokens, source_indices)
        target_tokens = _gather_tokens(object_tokens, target_indices)
        relation_gate = self.source_target_gate(torch.cat([state_token.unsqueeze(1).expand(bsz, k, -1), source_tokens, target_tokens], dim=-1))
        target_gate = self.target_gate(torch.cat([source_tokens, target_tokens, source_tokens - target_tokens], dim=-1))
        relation_tokens = self.relation_norm(relation_gate * source_tokens + target_gate * target_tokens)

        tokens = self.option_feature_encoder(option_features)
        tokens = tokens + self.option_kind_embedding(_bucket_ids(kind_ids, 64))
        tokens = tokens + self.card_embedding(_bucket_ids(card_ids, self.card_vocab_size))
        tokens = tokens + self._text_tokens(card_ids)
        tokens = tokens + self.row_embedding(_bucket_ids(row_ids, 32))
        tokens = tokens + self.insert_position_embedding(_bucket_ids(insert_positions, 32))
        tokens = tokens + self.slot_embedding(_bucket_ids(hand_slots, 512))
        tokens = tokens + relation_tokens
        tokens = self.option_norm(tokens + state_token.unsqueeze(1))
        tokens = tokens * option_mask.to(tokens.dtype).unsqueeze(-1)
        return tokens, option_mask, source_tokens, target_tokens

    def forward(self, batch: Mapping[str, torch.Tensor]) -> PolicyOutput:
        device = batch["global_features"].device
        object_tokens, object_mask = self._object_tokens(batch, device)
        state_token, pooled_objects, global_source_token = self._state_token(batch, object_tokens, object_mask, device)
        candidate_tokens, option_mask, option_source_tokens, option_target_tokens = self._option_tokens(batch, object_tokens, state_token, device)

        object_padding_mask = _safe_key_padding_mask(object_mask)
        option_padding_mask = _safe_key_padding_mask(option_mask)
        for object_attn, self_attn, ffn in zip(self.candidate_object_attention, self.candidate_self_attention, self.candidate_ffn):
            if self.use_candidate_object_attention:
                attended_objects, _ = object_attn(
                    query=candidate_tokens,
                    key=object_tokens,
                    value=object_tokens,
                    key_padding_mask=object_padding_mask,
                    need_weights=False,
                )
                candidate_tokens = self.option_norm(candidate_tokens + attended_objects)
            if self.use_candidate_self_attention:
                compared_options, _ = self_attn(
                    query=candidate_tokens,
                    key=candidate_tokens,
                    value=candidate_tokens,
                    key_padding_mask=option_padding_mask,
                    need_weights=False,
                )
                candidate_tokens = self.option_norm(candidate_tokens + compared_options)
            candidate_tokens = candidate_tokens + ffn(candidate_tokens)
            candidate_tokens = candidate_tokens * option_mask.to(candidate_tokens.dtype).unsqueeze(-1)

        bsz, max_options, _ = candidate_tokens.shape
        state_expanded = state_token.unsqueeze(1).expand(bsz, max_options, -1)
        pooled_expanded = pooled_objects.unsqueeze(1).expand(bsz, max_options, -1)
        source_expanded = global_source_token.unsqueeze(1).expand(bsz, max_options, -1)
        logit_inputs = torch.cat(
            [candidate_tokens, state_expanded, pooled_expanded, option_source_tokens, option_target_tokens + source_expanded],
            dim=-1,
        )
        logits = self.logit_head(logit_inputs).squeeze(-1)
        logits = masked_logits(logits, option_mask)

        legal_candidate_context = _masked_mean(candidate_tokens, option_mask)
        values = self.value_head(torch.cat([state_token, pooled_objects, global_source_token, legal_candidate_context], dim=-1)).squeeze(-1)
        return PolicyOutput(logits=logits, values=values)

    def act(self, batch: Mapping[str, torch.Tensor], deterministic: bool = False):
        out = self.forward(batch)
        if deterministic:
            actions = greedy_actions(out.logits, batch["option_mask"])
            dist = torch.distributions.Categorical(logits=out.logits)
            log_probs = dist.log_prob(actions)
            entropy = dist.entropy()
        else:
            actions, log_probs, entropy = sample_actions(out.logits, batch["option_mask"])
        return actions, log_probs, entropy, out.values

    def evaluate_actions(self, batch: Mapping[str, torch.Tensor], actions: torch.Tensor):
        out = self.forward(batch)
        dist = torch.distributions.Categorical(logits=out.logits)
        return dist.log_prob(actions), dist.entropy(), out.values


class SharedDeckHeadsPolicyValueNet(CandidatePolicyValueNet):
    """Shared Gwent backbone with deck-specific policy/value heads.

    Deck A and Deck B share every representation layer (global/object/option
    encoders, embeddings, prefix GRU, and attention blocks).  Only the final
    candidate scorer and critic are deck-specific.  The active head is selected
    per row from ``batch["actor_deck_ids"]`` (0 = deck A, 1 = deck B).

    The RL observation contract remains schema-compatible: ``actor_deck_ids``
    is Python-side routing metadata supplied by :class:`RlCollector`, not a new
    C observation feature.
    """

    ARCHITECTURE = "shared_deck_heads_v1"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        import copy

        base_logit = self.logit_head
        base_value = self.value_head
        self.logit_heads = nn.ModuleDict({
            "a": copy.deepcopy(base_logit),
            "b": copy.deepcopy(base_logit),
        })
        self.value_heads = nn.ModuleDict({
            "a": copy.deepcopy(base_value),
            "b": copy.deepcopy(base_value),
        })
        del self.logit_head
        del self.value_head

    def model_manifest(self) -> dict[str, object]:
        manifest = super().model_manifest()
        manifest.update({
            "architecture": self.ARCHITECTURE,
            "deck_head_ids": {"a": 0, "b": 1},
            "shared_backbone": True,
        })
        return manifest

    @staticmethod
    def _deck_ids(batch: Mapping[str, torch.Tensor], device: torch.device, batch_size: int) -> torch.Tensor:
        value = batch.get("actor_deck_ids")
        if value is None:
            raise ValueError(
                "shared_deck_heads_v1 requires batch['actor_deck_ids']; "
                "use RlCollector/RolloutBuffer from gwent_v2 or add routing metadata explicitly"
            )
        ids = value.to(device=device, dtype=torch.long).reshape(-1)
        if ids.shape[0] != batch_size:
            raise ValueError(f"actor_deck_ids size {ids.shape[0]} != batch size {batch_size}")
        invalid = (ids < 0) | (ids > 1)
        if invalid.any():
            bad = sorted(set(int(x) for x in ids[invalid].detach().cpu().tolist()))
            raise ValueError(f"unsupported actor_deck_ids: {bad}; expected 0 (A) or 1 (B)")
        return ids

    @staticmethod
    def _route_head_rows(
        inputs: torch.Tensor,
        deck_ids: torch.Tensor,
        heads: nn.ModuleDict,
    ) -> torch.Tensor:
        output_shape = (*inputs.shape[:-1], 1)
        output = torch.empty(output_shape, dtype=inputs.dtype, device=inputs.device)
        for deck_id, key in ((0, "a"), (1, "b")):
            rows = torch.nonzero(deck_ids == deck_id, as_tuple=False).flatten()
            if rows.numel() == 0:
                continue
            selected = inputs.index_select(0, rows)
            routed = heads[key](selected)
            output.index_copy_(0, rows, routed)
        return output

    def backbone_parameters(self) -> Iterable[nn.Parameter]:
        for name, parameter in self.named_parameters():
            if not name.startswith("logit_heads.") and not name.startswith("value_heads."):
                yield parameter

    def set_trainable_scope(self, scope: str = "joint") -> None:
        """Select which parameters PPO may update.

        ``joint`` updates the shared backbone and both deck heads.
        ``a_head_only`` / ``b_head_only`` freeze the backbone and the other
        deck's heads, which is the safe focused-training mode.
        """
        scope = str(scope).strip().lower().replace("-", "_")
        if scope not in {"joint", "a_head_only", "b_head_only"}:
            raise ValueError("scope must be joint, a_head_only, or b_head_only")
        for parameter in self.parameters():
            parameter.requires_grad_(scope == "joint")
        if scope == "joint":
            return
        key = "a" if scope.startswith("a_") else "b"
        for parameter in self.logit_heads[key].parameters():
            parameter.requires_grad_(True)
        for parameter in self.value_heads[key].parameters():
            parameter.requires_grad_(True)

    def forward(self, batch: Mapping[str, torch.Tensor]) -> PolicyOutput:
        device = batch["global_features"].device
        object_tokens, object_mask = self._object_tokens(batch, device)
        state_token, pooled_objects, global_source_token = self._state_token(
            batch, object_tokens, object_mask, device
        )
        candidate_tokens, option_mask, option_source_tokens, option_target_tokens = self._option_tokens(
            batch, object_tokens, state_token, device
        )

        object_padding_mask = _safe_key_padding_mask(object_mask)
        option_padding_mask = _safe_key_padding_mask(option_mask)
        for object_attn, self_attn, ffn in zip(
            self.candidate_object_attention, self.candidate_self_attention, self.candidate_ffn
        ):
            if self.use_candidate_object_attention:
                attended_objects, _ = object_attn(
                    query=candidate_tokens,
                    key=object_tokens,
                    value=object_tokens,
                    key_padding_mask=object_padding_mask,
                    need_weights=False,
                )
                candidate_tokens = self.option_norm(candidate_tokens + attended_objects)
            if self.use_candidate_self_attention:
                compared_options, _ = self_attn(
                    query=candidate_tokens,
                    key=candidate_tokens,
                    value=candidate_tokens,
                    key_padding_mask=option_padding_mask,
                    need_weights=False,
                )
                candidate_tokens = self.option_norm(candidate_tokens + compared_options)
            candidate_tokens = candidate_tokens + ffn(candidate_tokens)
            candidate_tokens = candidate_tokens * option_mask.to(candidate_tokens.dtype).unsqueeze(-1)

        bsz, max_options, _ = candidate_tokens.shape
        deck_ids = self._deck_ids(batch, device, bsz)
        state_expanded = state_token.unsqueeze(1).expand(bsz, max_options, -1)
        pooled_expanded = pooled_objects.unsqueeze(1).expand(bsz, max_options, -1)
        source_expanded = global_source_token.unsqueeze(1).expand(bsz, max_options, -1)
        logit_inputs = torch.cat(
            [candidate_tokens, state_expanded, pooled_expanded, option_source_tokens, option_target_tokens + source_expanded],
            dim=-1,
        )
        logits = self._route_head_rows(logit_inputs, deck_ids, self.logit_heads).squeeze(-1)
        logits = masked_logits(logits, option_mask)

        legal_candidate_context = _masked_mean(candidate_tokens, option_mask)
        value_inputs = torch.cat(
            [state_token, pooled_objects, global_source_token, legal_candidate_context], dim=-1
        )
        values = self._route_head_rows(value_inputs, deck_ids, self.value_heads).squeeze(-1)
        return PolicyOutput(logits=logits, values=values)


class SharedDeckPrivatePolicyValueNet(SharedDeckHeadsPolicyValueNet):
    """V3: shared semantic encoders + independent A/B strategy trunks.

    The active path is designed to be approximately 50% shared and 50% private
    (counting trainable parameters with the standard 1024-d frozen text table).
    Shared layers encode card/zone/row/option semantics.  Each deck owns its
    global/prefix context processing, relation gates, attention/FFN stack,
    residual strategy adapters, policy head, and value head.

    ``actor_deck_ids`` keeps the same Python-side routing contract as V2.
    """

    ARCHITECTURE = "shared_deck_private_v3"
    TARGET_PRIVATE_FRACTION = 0.50

    _PRIVATE_ROOTS = {
        "global_encoders",
        "decision_embeddings",
        "prefix_grus",
        "source_missing_by_deck",
        "state_norms",
        "prefix_norms",
        "relation_norms",
        "option_norms",
        "source_target_gates",
        "target_gates",
        "candidate_object_attentions",
        "candidate_self_attentions",
        "candidate_ffns",
        "state_adapters",
        "candidate_adapters",
        "logit_heads",
        "value_heads",
    }

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        import copy

        # V2 created one shared high-level strategy trunk.  V3 duplicates that
        # trunk before deleting the shared copy.  Low-level semantic encoders
        # and embeddings remain shared.
        self.global_encoders = nn.ModuleDict({
            "a": copy.deepcopy(self.global_encoder),
            "b": copy.deepcopy(self.global_encoder),
        })
        self.decision_embeddings = nn.ModuleDict({
            "a": copy.deepcopy(self.decision_embedding),
            "b": copy.deepcopy(self.decision_embedding),
        })
        self.prefix_grus = nn.ModuleDict({
            "a": copy.deepcopy(self.prefix_gru),
            "b": copy.deepcopy(self.prefix_gru),
        })
        self.source_missing_by_deck = nn.ParameterDict({
            "a": nn.Parameter(self.source_missing.detach().clone()),
            "b": nn.Parameter(self.source_missing.detach().clone()),
        })
        self.state_norms = nn.ModuleDict({
            "a": copy.deepcopy(self.state_norm),
            "b": copy.deepcopy(self.state_norm),
        })
        self.prefix_norms = nn.ModuleDict({
            "a": copy.deepcopy(self.prefix_norm),
            "b": copy.deepcopy(self.prefix_norm),
        })
        self.relation_norms = nn.ModuleDict({
            "a": copy.deepcopy(self.relation_norm),
            "b": copy.deepcopy(self.relation_norm),
        })
        self.option_norms = nn.ModuleDict({
            "a": copy.deepcopy(self.option_norm),
            "b": copy.deepcopy(self.option_norm),
        })
        self.source_target_gates = nn.ModuleDict({
            "a": copy.deepcopy(self.source_target_gate),
            "b": copy.deepcopy(self.source_target_gate),
        })
        self.target_gates = nn.ModuleDict({
            "a": copy.deepcopy(self.target_gate),
            "b": copy.deepcopy(self.target_gate),
        })
        self.candidate_object_attentions = nn.ModuleDict({
            "a": copy.deepcopy(self.candidate_object_attention),
            "b": copy.deepcopy(self.candidate_object_attention),
        })
        self.candidate_self_attentions = nn.ModuleDict({
            "a": copy.deepcopy(self.candidate_self_attention),
            "b": copy.deepcopy(self.candidate_self_attention),
        })
        self.candidate_ffns = nn.ModuleDict({
            "a": copy.deepcopy(self.candidate_ffn),
            "b": copy.deepcopy(self.candidate_ffn),
        })

        # Two zero-initialized residual adapters give each deck additional
        # strategy capacity without perturbing imported teacher behavior at
        # initialization.  With hidden=128 and text projection 1024->128 this
        # keeps each active path very close to a 50/50 trainable split.
        self.state_adapters = nn.ModuleDict({
            "a": self._make_private_adapter(expansion=4),
            "b": self._make_private_adapter(expansion=4),
        })
        self.candidate_adapters = nn.ModuleDict({
            "a": self._make_private_adapter(expansion=2),
            "b": self._make_private_adapter(expansion=2),
        })

        # Remove the V2 shared high-level trunk.  The V2 heads are intentionally
        # retained: they are already separate and form part of each private path.
        for name in (
            "global_encoder",
            "decision_embedding",
            "prefix_gru",
            "source_missing",
            "state_norm",
            "prefix_norm",
            "relation_norm",
            "option_norm",
            "source_target_gate",
            "target_gate",
            "candidate_object_attention",
            "candidate_self_attention",
            "candidate_ffn",
        ):
            delattr(self, name)

    def _make_private_adapter(self, *, expansion: int) -> nn.Sequential:
        width = self.hidden_dim * int(expansion)
        block = nn.Sequential(
            nn.LayerNorm(self.hidden_dim),
            nn.Linear(self.hidden_dim, width),
            nn.GELU(),
            nn.Dropout(self.dropout),
            nn.Linear(width, self.hidden_dim),
        )
        # Residual adapter starts as an exact identity transformation.
        with torch.no_grad():
            block[-1].weight.zero_()
            block[-1].bias.zero_()
        return block

    @classmethod
    def state_key_partition(cls, key: str) -> str:
        root = str(key).split(".", 1)[0]
        if root not in cls._PRIVATE_ROOTS:
            return "shared"
        parts = str(key).split(".")
        if len(parts) >= 2 and parts[1] in {"a", "b"}:
            return f"{parts[1]}_private"
        raise ValueError(f"cannot determine V3 private partition for state key {key!r}")

    def shared_parameters(self) -> Iterable[nn.Parameter]:
        for name, parameter in self.named_parameters():
            if self.state_key_partition(name) == "shared":
                yield parameter

    def private_parameters(self, deck: str) -> Iterable[nn.Parameter]:
        key = str(deck).strip().lower()
        if key not in {"a", "b"}:
            raise ValueError("deck must be 'a' or 'b'")
        wanted = f"{key}_private"
        for name, parameter in self.named_parameters():
            if self.state_key_partition(name) == wanted:
                yield parameter

    def parameter_partition_report(
        self,
        *,
        trainable_only: bool = True,
        respect_current_scope: bool = False,
    ) -> dict[str, float | int]:
        counts = {"shared": 0, "a_private": 0, "b_private": 0}
        for name, parameter in self.named_parameters():
            if trainable_only and name == "text_embedding.weight" and self.freeze_text_embeddings:
                continue
            if respect_current_scope and not parameter.requires_grad:
                continue
            partition = self.state_key_partition(name)
            counts[partition] += parameter.numel()

        shared = counts["shared"]
        a_private = counts["a_private"]
        b_private = counts["b_private"]
        a_active = shared + a_private
        b_active = shared + b_private
        return {
            "shared": shared,
            "a_private": a_private,
            "b_private": b_private,
            "a_active": a_active,
            "b_active": b_active,
            "a_shared_fraction": float(shared / max(a_active, 1)),
            "a_private_fraction": float(a_private / max(a_active, 1)),
            "b_shared_fraction": float(shared / max(b_active, 1)),
            "b_private_fraction": float(b_private / max(b_active, 1)),
        }

    def model_manifest(self) -> dict[str, object]:
        manifest = CandidatePolicyValueNet.model_manifest(self)
        report = self.parameter_partition_report(trainable_only=True)
        manifest.update({
            "architecture": self.ARCHITECTURE,
            "deck_head_ids": {"a": 0, "b": 1},
            "shared_backbone": True,
            "deck_private_strategy": True,
            "target_private_fraction_per_active_path": self.TARGET_PRIVATE_FRACTION,
            "parameter_partition_trainable": report,
            "private_components": [
                "global_context",
                "prefix_gru",
                "relation_gates",
                "candidate_attention",
                "candidate_ffn",
                "strategy_adapters",
                "policy_head",
                "value_head",
            ],
        })
        return manifest

    def backbone_parameters(self) -> Iterable[nn.Parameter]:
        # Compatibility name used by older V2 tooling: in V3 this means the
        # genuinely shared semantic encoder only.
        yield from self.shared_parameters()

    def set_trainable_scope(self, scope: str = "joint") -> None:
        """Select trainable V3 partitions.

        ``a_head_only`` and ``b_head_only`` remain accepted aliases for old V2
        commands, but in V3 they intentionally mean the whole deck-private 50%.
        """
        normalized = str(scope).strip().lower().replace("-", "_")
        aliases = {
            "a_head_only": "a_private_only",
            "b_head_only": "b_private_only",
        }
        normalized = aliases.get(normalized, normalized)
        allowed = {"joint", "a_private_only", "b_private_only", "shared_only"}
        if normalized not in allowed:
            raise ValueError(
                "scope must be joint, a_private_only, b_private_only, or shared_only"
            )
        for parameter in self.parameters():
            parameter.requires_grad_(False)
        if normalized == "joint":
            for parameter in self.parameters():
                parameter.requires_grad_(True)
            if self.text_embedding is not None and self.freeze_text_embeddings:
                self.text_embedding.weight.requires_grad_(False)
            return
        if normalized == "shared_only":
            for parameter in self.shared_parameters():
                parameter.requires_grad_(True)
            if self.text_embedding is not None and self.freeze_text_embeddings:
                self.text_embedding.weight.requires_grad_(False)
            return
        deck = "a" if normalized.startswith("a_") else "b"
        for parameter in self.private_parameters(deck):
            parameter.requires_grad_(True)

    @staticmethod
    def _select_batch_rows(
        batch: Mapping[str, torch.Tensor], rows: torch.Tensor, batch_size: int
    ) -> dict[str, torch.Tensor]:
        selected: dict[str, torch.Tensor] = {}
        for key, value in batch.items():
            if isinstance(value, torch.Tensor) and value.ndim > 0 and value.shape[0] == batch_size:
                selected[key] = value.index_select(0, rows)
            else:
                selected[key] = value
        return selected

    def _prefix_context_private(
        self,
        deck: str,
        batch: Mapping[str, torch.Tensor],
        object_tokens: torch.Tensor,
        device: torch.device,
    ) -> torch.Tensor:
        bsz = object_tokens.shape[0]
        prefix_mask = batch.get("prefix_mask")
        if prefix_mask is None:
            return torch.zeros((bsz, self.hidden_dim), dtype=object_tokens.dtype, device=device)
        prefix_mask = prefix_mask.to(device=device, dtype=torch.bool)
        p = prefix_mask.shape[1]
        prefix_kind_ids = _ids(batch, "prefix_kind_ids", (bsz, p), device)
        prefix_row_ids = _ids(batch, "prefix_row_ids", (bsz, p), device)
        prefix_insert_positions = _ids(batch, "prefix_insert_positions", (bsz, p), device)
        prefix_source = _ids(batch, "prefix_source_object_indices", (bsz, p), device)
        prefix_target = _ids(batch, "prefix_target_object_indices", (bsz, p), device)
        prefix_card_ids = _ids(batch, "prefix_card_ids", (bsz, p), device)
        source_tokens = _gather_tokens(object_tokens, prefix_source)
        target_tokens = _gather_tokens(object_tokens, prefix_target)
        tokens = self.prefix_kind_embedding(_bucket_ids(prefix_kind_ids, 64))
        tokens = tokens + self.row_embedding(_bucket_ids(prefix_row_ids, 32))
        tokens = tokens + self.insert_position_embedding(_bucket_ids(prefix_insert_positions, 32))
        prefix_card_tokens = self.card_embedding(_bucket_ids(prefix_card_ids, self.card_vocab_size))
        prefix_card_tokens = prefix_card_tokens + self._text_tokens(prefix_card_ids)
        prefix_card_tokens = prefix_card_tokens * (prefix_card_ids >= 0).to(tokens.dtype).unsqueeze(-1)
        tokens = tokens + prefix_card_tokens + source_tokens + target_tokens
        positions = torch.arange(p, device=device).view(1, p).expand(bsz, p)
        positions = positions.clamp(max=max(self.max_prefix - 1, 0))
        tokens = tokens + self.prefix_position_embedding(positions)
        tokens = self.prefix_norms[deck](tokens)
        tokens = tokens * prefix_mask.to(tokens.dtype).unsqueeze(-1)
        encoded, _ = self.prefix_grus[deck](tokens)
        lengths = prefix_mask.sum(dim=1)
        last_index = (lengths - 1).clamp_min(0)
        context = encoded[torch.arange(bsz, device=device), last_index]
        return context * (lengths > 0).to(context.dtype).unsqueeze(-1)

    def _state_token_private(
        self,
        deck: str,
        batch: Mapping[str, torch.Tensor],
        object_tokens: torch.Tensor,
        object_mask: torch.Tensor,
        device: torch.device,
    ) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        global_features = batch["global_features"].to(device=device, dtype=torch.float32)
        state_token = self.global_encoders[deck](global_features)
        decision_kinds = _ids(batch, "decision_kinds", (global_features.shape[0],), device)
        state_token = state_token + self.decision_embeddings[deck](_bucket_ids(decision_kinds, 32))
        pooled_objects = _masked_mean(object_tokens, object_mask)
        source_indices = _ids(batch, "source_object_indices", (global_features.shape[0],), device)
        source_token = _gather_tokens(object_tokens, source_indices)
        missing = (source_indices < 0).to(dtype=source_token.dtype).unsqueeze(-1)
        source_token = source_token + missing * self.source_missing_by_deck[deck].view(1, -1)
        prefix_context = self._prefix_context_private(deck, batch, object_tokens, device)
        state_token = self.state_norms[deck](state_token + source_token + prefix_context)
        state_token = state_token + self.state_adapters[deck](state_token)
        return state_token, pooled_objects, source_token

    def _option_tokens_private(
        self,
        deck: str,
        batch: Mapping[str, torch.Tensor],
        object_tokens: torch.Tensor,
        state_token: torch.Tensor,
        device: torch.device,
    ) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
        option_features = batch["option_features"].to(device=device, dtype=torch.float32)
        option_mask = batch["option_mask"].to(device=device, dtype=torch.bool)
        bsz, k, _ = option_features.shape
        kind_ids = _ids(batch, "option_kind_ids", (bsz, k), device)
        card_ids = _ids(batch, "option_card_ids", (bsz, k), device)
        row_ids = _ids(batch, "option_target_row_ids", (bsz, k), device)
        insert_positions = _ids(batch, "option_insert_positions", (bsz, k), device)
        hand_slots = _ids(batch, "option_hand_slot_indices", (bsz, k), device)
        source_indices = _ids(batch, "option_source_object_indices", (bsz, k), device)
        target_indices = _ids(batch, "option_target_object_indices", (bsz, k), device)
        source_tokens = _gather_tokens(object_tokens, source_indices)
        target_tokens = _gather_tokens(object_tokens, target_indices)
        relation_gate = self.source_target_gates[deck](
            torch.cat([state_token.unsqueeze(1).expand(bsz, k, -1), source_tokens, target_tokens], dim=-1)
        )
        target_gate = self.target_gates[deck](
            torch.cat([source_tokens, target_tokens, source_tokens - target_tokens], dim=-1)
        )
        relation_tokens = self.relation_norms[deck](
            relation_gate * source_tokens + target_gate * target_tokens
        )
        tokens = self.option_feature_encoder(option_features)
        tokens = tokens + self.option_kind_embedding(_bucket_ids(kind_ids, 64))
        tokens = tokens + self.card_embedding(_bucket_ids(card_ids, self.card_vocab_size))
        tokens = tokens + self._text_tokens(card_ids)
        tokens = tokens + self.row_embedding(_bucket_ids(row_ids, 32))
        tokens = tokens + self.insert_position_embedding(_bucket_ids(insert_positions, 32))
        tokens = tokens + self.slot_embedding(_bucket_ids(hand_slots, 512))
        tokens = tokens + relation_tokens
        tokens = self.option_norms[deck](tokens + state_token.unsqueeze(1))
        tokens = tokens * option_mask.to(tokens.dtype).unsqueeze(-1)
        return tokens, option_mask, source_tokens, target_tokens

    def _forward_deck(
        self,
        deck: str,
        batch: Mapping[str, torch.Tensor],
        object_tokens: torch.Tensor,
        object_mask: torch.Tensor,
    ) -> PolicyOutput:
        device = object_tokens.device
        state_token, pooled_objects, global_source_token = self._state_token_private(
            deck, batch, object_tokens, object_mask, device
        )
        candidate_tokens, option_mask, option_source_tokens, option_target_tokens = self._option_tokens_private(
            deck, batch, object_tokens, state_token, device
        )
        object_padding_mask = _safe_key_padding_mask(object_mask)
        option_padding_mask = _safe_key_padding_mask(option_mask)
        for object_attn, self_attn, ffn in zip(
            self.candidate_object_attentions[deck],
            self.candidate_self_attentions[deck],
            self.candidate_ffns[deck],
        ):
            if self.use_candidate_object_attention:
                attended_objects, _ = object_attn(
                    query=candidate_tokens,
                    key=object_tokens,
                    value=object_tokens,
                    key_padding_mask=object_padding_mask,
                    need_weights=False,
                )
                candidate_tokens = self.option_norms[deck](candidate_tokens + attended_objects)
            if self.use_candidate_self_attention:
                compared_options, _ = self_attn(
                    query=candidate_tokens,
                    key=candidate_tokens,
                    value=candidate_tokens,
                    key_padding_mask=option_padding_mask,
                    need_weights=False,
                )
                candidate_tokens = self.option_norms[deck](candidate_tokens + compared_options)
            candidate_tokens = candidate_tokens + ffn(candidate_tokens)
            candidate_tokens = candidate_tokens * option_mask.to(candidate_tokens.dtype).unsqueeze(-1)
        candidate_tokens = candidate_tokens + self.candidate_adapters[deck](candidate_tokens)
        candidate_tokens = candidate_tokens * option_mask.to(candidate_tokens.dtype).unsqueeze(-1)

        bsz, max_options, _ = candidate_tokens.shape
        state_expanded = state_token.unsqueeze(1).expand(bsz, max_options, -1)
        pooled_expanded = pooled_objects.unsqueeze(1).expand(bsz, max_options, -1)
        source_expanded = global_source_token.unsqueeze(1).expand(bsz, max_options, -1)
        logit_inputs = torch.cat(
            [
                candidate_tokens,
                state_expanded,
                pooled_expanded,
                option_source_tokens,
                option_target_tokens + source_expanded,
            ],
            dim=-1,
        )
        logits = self.logit_heads[deck](logit_inputs).squeeze(-1)
        logits = masked_logits(logits, option_mask)
        legal_candidate_context = _masked_mean(candidate_tokens, option_mask)
        value_inputs = torch.cat(
            [state_token, pooled_objects, global_source_token, legal_candidate_context], dim=-1
        )
        values = self.value_heads[deck](value_inputs).squeeze(-1)
        return PolicyOutput(logits=logits, values=values)

    def forward(self, batch: Mapping[str, torch.Tensor]) -> PolicyOutput:
        device = batch["global_features"].device
        batch_size = int(batch["global_features"].shape[0])
        deck_ids = self._deck_ids(batch, device, batch_size)
        object_tokens, object_mask = self._object_tokens(batch, device)
        max_options = int(batch["option_mask"].shape[1])
        logits = torch.empty((batch_size, max_options), dtype=object_tokens.dtype, device=device)
        values = torch.empty((batch_size,), dtype=object_tokens.dtype, device=device)

        for deck_id, deck in ((0, "a"), (1, "b")):
            rows = torch.nonzero(deck_ids == deck_id, as_tuple=False).flatten()
            if rows.numel() == 0:
                continue
            sub_batch = self._select_batch_rows(batch, rows, batch_size)
            sub_objects = object_tokens.index_select(0, rows)
            sub_object_mask = object_mask.index_select(0, rows)
            out = self._forward_deck(deck, sub_batch, sub_objects, sub_object_mask)
            logits.index_copy_(0, rows, out.logits)
            values.index_copy_(0, rows, out.values)
        return PolicyOutput(logits=logits, values=values)


class MeanPoolCandidatePolicyValueNet(CandidatePolicyValueNet):
    """Backward-compatible import target for older experiment code."""

    pass
