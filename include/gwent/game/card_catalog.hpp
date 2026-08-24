#pragma once

// Compatibility include. CardCatalog is core-owned because GameState shares it
// across runtime snapshots; existing callers may continue including this path.
#include "gwent/core/card_catalog.hpp"
