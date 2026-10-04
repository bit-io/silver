# Svelte 5 (Silver 0.2)

Komponent Svelte (runes: `$state`, `$derived`, `$effect`) kompilowany przez `esbuild-svelte`
do jednego pliku IIFE. Backend w H# (`src/main.h#`).

```sh
bit             # npm install + build (hook pre-build) → kompilacja H# → uruchomienie
bit watch       # w drugim terminalu: przebudowa przy zapisie
```
Działa na lustrze DOM Silvera (patrz docs/javascript.md); komponent korzysta z `silver.invoke`.
