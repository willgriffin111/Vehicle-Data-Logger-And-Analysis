import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';


export default defineConfig({
  plugins: [svelte()],
  build: {
    outDir: 'dist',
  },
  server: {
    fs: {
      strict: false,
    },
  },
});
