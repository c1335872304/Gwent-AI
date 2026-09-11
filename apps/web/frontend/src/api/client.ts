export class HttpError extends Error {
  constructor(
    public readonly status: number,
    public readonly code: string | null,
    message: string,
  ) {
    super(message)
    this.name = "HttpError"
  }
}

export async function requestJson<T>(url: string, init?: RequestInit): Promise<T> {
  const response = await fetch(url, init)

  if (!response.ok) {
    let detail = `${response.status} ${response.statusText}`
    let code: string | null = null
    try {
      const body = await response.json()
      const error = body.detail ?? body
      if (typeof error === "object" && error !== null) {
        detail = error.message ?? JSON.stringify(error)
        code = error.code ?? null
      } else {
        detail = String(error)
      }
    } catch {
      // Keep the HTTP status as the fallback message.
    }
    throw new HttpError(response.status, code, detail)
  }

  return response.json() as Promise<T>
}
