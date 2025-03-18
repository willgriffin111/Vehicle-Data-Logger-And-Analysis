import { test, expect, request } from '@playwright/test';

test('Days endpoint returns valid response', async () => {
  const apiContext = await request.newContext();
  const response = await apiContext.get('http://192.168.4.1/days');
  expect(response.status()).toBe(200);
  const json = await response.json();
  expect(Array.isArray(json)).toBe(true);
  expect(json.length).toBeGreaterThan(0);
  expect(json).toContain('test');
});

test('Drives endpoint returns valid response for day "test"', async () => {
  const apiContext = await request.newContext();
  const response = await apiContext.get('http://192.168.4.1/drives?day=test');
  expect(response.status()).toBe(200);
  const json = await response.json();
  expect(Array.isArray(json)).toBe(true);
  expect(json.length).toBeGreaterThan(0);
  expect(json).toContain('dummy.json');
});

test('Drive endpoint returns valid drive data for day "test" and drive "dummy.json"', async () => {
  const apiContext = await request.newContext();
  const response = await apiContext.get('http://192.168.4.1/drive?day=test&drive=dummy.json');
  expect(response.status()).toBe(200);
  const text = await response.text();
  const lines = text.split('\n').filter(line => line.trim() !== '');
  expect(lines.length).toBeGreaterThan(0);
  for (const line of lines) {
    const data = JSON.parse(line);
    expect(data).toHaveProperty('gps');
    expect(data).toHaveProperty('obd');
    expect(data).toHaveProperty('imu');
    expect(data.gps).toHaveProperty('time');
    expect(data.gps).toHaveProperty('latitude');
    expect(data.gps).toHaveProperty('longitude');
    expect(data.obd).toHaveProperty('rpm');
    expect(data.obd).toHaveProperty('speed');
    expect(data.obd).toHaveProperty('maf');
    expect(data.obd).toHaveProperty('instant_mpg');
    expect(data.obd).toHaveProperty('throttle');
    expect(data.obd).toHaveProperty('avg_mpg');
    expect(data.imu).toHaveProperty('accel_x');
    expect(data.imu).toHaveProperty('accel_y');
  }
});

test('SD Info endpoint returns valid diagnostics', async () => {
  const apiContext = await request.newContext();
  const response = await apiContext.get('http://192.168.4.1/sdinfo');
  expect(response.status()).toBe(200);
  const json = await response.json();
  expect(json).toHaveProperty('sd_status');
  expect(json).toHaveProperty('total_size');
  expect(json).toHaveProperty('used_size');
  expect(json).toHaveProperty('free_size');
  expect(json).toHaveProperty('esp32_uptime_sec');
});

test.describe.serial('Delete drive file tests', () => {
  test('Drive file exists before deletion', async ({ request }) => {
    const response = await request.get('http://192.168.4.1/drive?day=test&drive=dummy.json');
    expect(response.status()).toBe(200);
  });
  test('Delete drive file successfully', async ({ request }) => {
    const deleteResponse = await request.delete('http://192.168.4.1/delete?path=/test/dummy.json');
    expect(deleteResponse.status()).toBe(200);
    const text = await deleteResponse.text();
    expect(text).toContain('Deleted successfully');
  });
  test('Drive file is not found after deletion', async ({ request }) => {
    const response = await request.get('http://192.168.4.1/drive?day=test&drive=dummy.json');
    expect(response.status()).toBe(404);
  });
});
