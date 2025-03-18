import { test, expect } from '@playwright/test';

test.describe.serial('Driving Analysis UI tests', () => {
  let page;
  
  test.beforeAll(async ({ browser }) => {
    page = await browser.newPage();
    await page.goto('http://localhost:5173', { timeout: 60000, waitUntil: 'domcontentloaded' });
    await page.click('#connection-status-container button');
    await page.waitForFunction(() => {
      const el = document.querySelector('#connection-status-container');
      return el && el.innerText.includes('Connected');
    }, { timeout: 10000 });
  });
  
  test.afterAll(async () => {
    await page.close();
  });
  
  test('Initial UI renders correctly', async () => {
    await expect(page.locator('h1')).toHaveText('Driving Analysis Interface');
    await expect(page.locator('#connection-status-container')).toContainText('Status: Connected');
    const modeSelect = page.locator('select.dropdown').first();
    await expect(modeSelect).toHaveValue('post');
    await expect(page.locator('#map')).toBeVisible();
  });
  
  test('UI displays "post" mode elements', async () => {
    await expect(page.locator('label:has-text("Select Day:") + select')).toBeVisible();
    await expect(page.locator('label:has-text("Select Drive:") + select')).toBeVisible();
    await expect(page.locator('button', { hasText: 'Load Drive' })).toBeVisible();
    await expect(page.locator('fieldset legend')).toHaveText('Route Data Overlay:');
    await expect(page.locator('button', { hasText: 'Hide Points' })).toBeVisible();
  });
  
  test('UI displays "real-time" mode elements', async () => {
    const modeSelect = page.locator('select.dropdown').first();
    await modeSelect.selectOption('real-time');
    await page.waitForTimeout(500);
    await expect(page.locator('#live-data-container')).toBeVisible();
    await expect(page.locator('#live-data-container')).toContainText('Time:');
    await expect(page.locator('#live-data-container')).toContainText('Speed:');
    await expect(page.locator('#live-data-container')).toContainText('RPM:');
    await expect(page.locator('button', { hasText: 'Hide Points' })).toBeVisible();
  });
  
  test('UI displays "Settings" mode elements', async () => {
    const modeSelect = page.locator('select.dropdown').first();
    await modeSelect.selectOption('Settings');
    await page.waitForTimeout(500);
    await expect(page.locator('label:has-text("Speed metric:") + select')).toBeVisible();
    await expect(page.locator('.sd-info-box')).toBeVisible();
    await expect(page.locator('.sd-info-box')).toContainText('SD Card Status:');
    await expect(page.locator('#delete-day-select')).toBeVisible();
    await expect(page.locator('#delete-drive-select')).toBeVisible();
    await expect(page.locator('button', { hasText: 'Delete All Drives for' })).toBeVisible();
    await expect(page.locator('button', { hasText: 'Delete Selected Drive on' })).toBeVisible();
    await expect(page.locator('button', { hasText: 'Load Drive' })).toBeVisible();
  });
  
  test('Connection check via Retry button updates status to Connected', async () => {
    await page.click('#connection-status-container button');
    await page.waitForFunction(() => {
      const el = document.querySelector('#connection-status-container');
      return el && el.innerText.includes('Connected');
    }, { timeout: 10000 });
    await expect(page.locator('#connection-status-container')).toContainText('Connected');
  });
  
  test('Toggling points changes button text', async () => {
    const modeSelect = page.locator('select.dropdown').first();
    await modeSelect.selectOption('post');
    await page.waitForTimeout(500);
    const toggleButton = page.locator('button', { hasText: 'Hide Points' });
    await expect(toggleButton).toBeVisible();
    await toggleButton.click();
    await expect(page.locator('button', { hasText: 'Show Points' })).toBeVisible();
  });
});
