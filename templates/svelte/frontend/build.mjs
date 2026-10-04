import esbuild from 'esbuild';
import sveltePlugin from 'esbuild-svelte';

const watch = process.argv.includes('--watch');
const ctx = await esbuild.context({
  entryPoints: ['src/main.js'],
  bundle: true,
  format: 'iife',
  target: 'es2020',
  plugins: [sveltePlugin({ compilerOptions: { dev: watch } })],
  mainFields: ['svelte', 'browser', 'module', 'main'],
  conditions: ['svelte', 'browser'],
  define: { 'process.env.NODE_ENV': watch ? '"development"' : '"production"' },
  minify: !watch,
  outfile: 'dist/app.js',
  logLevel: 'info',
});
if (watch) { await ctx.watch(); console.log('obserwuję zmiany…'); }
else { await ctx.rebuild(); await ctx.dispose(); }
