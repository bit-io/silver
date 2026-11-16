// Test typów: kompiluje się tylko wtedy, gdy deklaracje są poprawne i użyteczne.
async function demo(): Promise<void> {
  const res = await silver.invoke<{ count: string }>("increment", { by: 1 });
  const n: number = Number(res.count);
  const off = silver.listen<{ output: string }>("silver://task", (p) => console.log(p.output));
  off();
  silver.once<SilverSecondInstancePayload>("silver://second-instance", (p) => console.log(p.args.split("\n")));
  await silver.window.setTitle("Tytuł " + n);
  await silver.window.setFrameless(8);
  const info = await silver.info();
  const backend: string = info.backend;
  const files: string[] = await silver.dialog.openMultiple({ pattern: "*.png" });
  const text: string = await silver.clipboard.readText();
  window.silver.registerSourceMap("app.js", "{}");
  console.log(backend, files.length, text.length, silver.version);
}
void demo();
// @ts-expect-error — nieistniejąca metoda musi być błędem typu
silver.window.nope();
