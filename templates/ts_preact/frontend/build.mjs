import esbuild from 'esbuild';

const watch = process.argv.includes('--watch');
const ctx = await esbuild.context({
  entryPoints: ['src/main.tsx'],
  bundle: true,
  format: 'iife',
  target: 'es2020',
  jsxFactory: 'h',
  jsxFragment: 'Fragment',
  define: { 'process.env.NODE_ENV': watch ? '"development"' : '"production"' },
  minify: !watch,
  sourcemap: false,
  outfile: 'dist/app.js',
  logLevel: 'info',
});
if (watch) { await ctx.watch(); console.log('obserwuję zmiany…'); }
else { await ctx.rebuild(); await ctx.dispose(); }
