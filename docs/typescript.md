# TypeScript, npm i frameworki

Silver nie parsuje `package.json` ani TypeScriptu — frontend buduje **esbuild** do jednego pliku IIFE (ES2020), który
ładuje `app::load_js`. CLI robi to za Ciebie: gdy istnieje `frontend/package.json`, `silver build/run/dev` wołają
`npm install` i `npm run build` / `npm run watch`, a `bit` ma hook `pre-build` z tym samym.

```sh
silver new notatki --ts            # TSX + Preact
silver new notatki --svelte        # Svelte 5
cd notatki && bit                  # npm install + build → kompilacja H# → start
bit watch                          # drugi terminal: przebudowa przy zapisie (hot reload podchwyci dist/app.js)
```

## Typy
`types/silver.d.ts` (kopiowany do szablonów jako `frontend/src/silver.d.ts`) opisuje `silver.invoke/listen/window/dialog/clipboard`.
`tsconfig.json` z `"lib": ["ES2020", "DOM"]` daje typy `document`, `fetch` itd. Kontrakt komend trzymaj w jednym pliku
(`frontend/src/api.ts` w szablonie) — typowane `call('save_notes', { notes })`.

## Sprawdzone
| Stos | Status |
|---|---|
| TypeScript + esbuild | zbudowane i uruchomione w QuickJS |
| Preact 10 (hooks, TSX) | działa: render, stan, zdarzenia, ponowny render |
| Svelte 4 (kompilator) | działa: reaktywność, `{#if}`, `{#each}` |
| Svelte 5 (runes, `bind:value`) | działa: `$state`, `$derived`, `$effect`, `mount` |
| React, Vue, Solid | niesprawdzone |

Powtórz: `sh native/tests/run-js.sh` (prawdziwy npm + esbuild, wymaga sieci).

## Wskazówki
- cel `--target=es2020`, `--format=iife`, `--define:process.env.NODE_ENV="production"`;
- pakiety używające Node API (`fs`, `path`, `http`) nie zadziałają — używaj `silver.invoke` zamiast tego;
- biblioteki, które mierzą układ (`getBoundingClientRect`), dostaną zera (patrz ROADMAP);
- `fetch` nie wspiera nagłówków; dla złożonych żądań zrób komendę w H#.
