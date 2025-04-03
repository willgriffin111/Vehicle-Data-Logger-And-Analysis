import { test, expect } from '@playwright/test';

test.beforeEach(async ({ page }) => {
  await page.goto('http://localhost:5173', { timeout: 60000, waitUntil: 'load' });
});

test('Initial UI renders correctly', async ({ page }) => {
  await expect(page.locator('h1')).toHaveText('Driving Analysis Interface');
  await expect(page.locator('#connection-status-container')).toContainText('Connected');
  const modeSelect = page.locator('select.dropdown').first();
  await expect(modeSelect).toHaveValue('post');
  await expect(page.locator('#map')).toBeVisible();
});

test('UI displays "post" mode elements', async ({ page }) => {
  await expect(page.locator('label:has-text("Select Day:") + select')).toBeVisible();
  await expect(page.locator('label:has-text("Select Drive:") + select')).toBeVisible();
  await expect(page.locator('button', { hasText: 'Load Drive' })).toBeVisible();
  await expect(page.locator('fieldset legend')).toHaveText('Route Data Overlay:');
  await expect(page.locator('button', { hasText: 'Hide Points' })).toBeVisible();
});


test('UI displays "real-time" mode elements', async ({ page }) => {
  const modeSelect = page.locator('select.dropdown').first();
  await modeSelect.selectOption('real-time');
  await page.waitForTimeout(500);
  await expect(page.locator('#live-data-container')).toBeVisible();
  await expect(page.locator('#live-data-container')).toContainText('Time:');
  await expect(page.locator('#live-data-container')).toContainText('Speed:');
  await expect(page.locator('#live-data-container')).toContainText('RPM:');
  await expect(page.locator('button', { hasText: 'Hide Points' })).toBeVisible();
});

test('UI displays "Settings" mode elements', async ({ page }) => {
  const modeSelect = page.locator('select.dropdown').first();
  await modeSelect.selectOption('Settings');
  await page.waitForTimeout(25000);
  await expect(page.locator('label:has-text("Speed metric:") + select')).toBeVisible();
  await expect(page.locator('.sd-info-box')).toBeVisible();
  await expect(page.locator('.sd-info-box')).toContainText('SD Card Status:');
  await expect(page.locator('#delete-day-select')).toBeVisible();
  await expect(page.locator('#delete-drive-select')).toBeVisible();
  await expect(page.locator('button', { hasText: 'Delete All Drives for' })).toBeVisible();
  await expect(page.locator('button', { hasText: 'Delete Selected Drive on' })).toBeVisible();
  await expect(page.locator('button', { hasText: 'Load Drive' })).toBeVisible();
});

test('Connection check via Retry button updates status to Connected', async ({ page }) => {
  await page.route('http://192.168.4.1/', async (route) => {
    await route.fulfill({
      status: 200,
      contentType: 'text/plain',
      body: 'Connected'
    });
  });
  await page.click('#connection-status-container button');
  await page.waitForTimeout(500);
  await expect(page.locator('#connection-status-container')).toContainText('Connected');
});

test('Toggling points changes button text', async ({ page }) => {
  const modeSelect = page.locator('select.dropdown').first();
  await modeSelect.selectOption('post');
  await page.waitForTimeout(1000);
  const toggleButton = page.locator('button', { hasText: /^(Hide Points|Show Points)$/ });
  await expect(toggleButton).toBeVisible();
  await toggleButton.click();
  await expect(page.locator('button', { hasText: 'Show Points' })).toBeVisible();
});

test('Map are visible after drive load', async ({ page }) => {
    const modeSelect = page.locator('select.dropdown').first();
    await modeSelect.selectOption('Post');
    await page.click('button:has-text("Load Drive")');
    await page.waitForTimeout(5000);
    await expect(page.locator('#map')).toBeVisible();
  });



