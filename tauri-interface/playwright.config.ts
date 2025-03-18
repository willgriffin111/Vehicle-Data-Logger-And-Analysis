import { defineConfig } from '@playwright/test';

export default defineConfig({
  webServer: {
    command: 'npm run dev',  
    port: 5173,
    timeout: 60000, 
    reuseExistingServer: true, 
  },
  testDir: 'tests', 
  workers: 1,
});
