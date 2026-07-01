import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],
  base: '/webui/',
  server: {
    proxy: {
      '/webui/api': {
        target: process.env.RADIANT_NODE_URL || 'http://127.0.0.1:7898',
        changeOrigin: true,
      }
    }
  },
  build: {
    // Deterministic asset names so embed_assets.py can handle them consistently.
    rollupOptions: {
      output: {
        entryFileNames:   'assets/[name].js',
        chunkFileNames:   'assets/[name].js',
        assetFileNames:   'assets/[name].[ext]',
      }
    }
  }
})
