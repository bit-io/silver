export interface Note { id: number; text: string; pinned: boolean; created: number }

interface Commands {
  list_notes: { args: undefined; result: { notes: Note[] } };
  save_notes: { args: { notes: Note[] }; result: { ok: boolean } };
}

export function call<K extends keyof Commands>(
  cmd: K,
  ...args: Commands[K]['args'] extends undefined ? [] : [Commands[K]['args']]
): Promise<Commands[K]['result']> {
  return silver.invoke(cmd, args[0] as Record<string, unknown> | undefined);
}
