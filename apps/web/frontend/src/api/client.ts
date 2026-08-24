export async function requestJson<T>(url: string, init?: RequestInit): Promise<T> {
  const response = await fetch(url, init)

  if (!response.ok) {
    let detail = `${response.status} ${response.statusText}`
    try {
      const body = await response.json()
      detail = body.detail ?? JSON.stringify(body)
    } catch {
      // Keep the HTTP status as the fallback message.
    }
    throw new Error(detail)
  }

  return response.json() as Promise<T>
}
