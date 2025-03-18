<script>
  import { onMount, tick } from "svelte";
  import L from "leaflet";
  import "leaflet/dist/leaflet.css";
  import Chart from "chart.js/auto";
  import { confirm } from '@tauri-apps/plugin-dialog';

  let map;
  let routeData = [];
  let markers = [];
  let showPoints = true;
  let selectedMetric = "default";

  let message = "Not Connected";
  let processingMode = "post";

  let days = [];
  let selectedDay = "";
  let drives = [];
  let selectedDrive = "";
  


  // Chart instances
  let chart1, chart2, chart3, chart4, chart5, chart6, chart7, chart8;
  // Canvas element references using bind:this
  let chart1Canvas, chart2Canvas, chart3Canvas, chart4Canvas, chart5Canvas, chart6Canvas, chart7Canvas, chart8Canvas;

  // Variables for live data
  let liveTime = "";
  let liveSpeed = "";
  let liveRPM = "";
  let liveInstantMPG = "";
  let liveAvgMPG = "";  
  let liveThrottle = "";
  let liveAccelX = "";
  let liveAccelY = "";

  let speedMetric = "mph";

  let sdStatus = "Loading...";
  let totalSize = "Loading...";
  let usedSize = "Loading...";
  let freeSize = "Loading...";
  let upTime = "Loading...";

  // Live data polling interval
  let liveDataInterval;

  async function checkConnection() {
    try {
      console.log("Fetching connection status...");
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 1000);
      const response = await fetch("http://192.168.4.1/", { signal: controller.signal });
      clearTimeout(timeout);
      if (!response.ok) throw new Error("Network response error");
      const text = await response.text();

      if (text.includes("Connected")) {
        message = "Connected";
        fetchDays();
      } else {
        message = "Not Connected";
      }
    } catch (error) {
      console.error("Fetch error:", error);
      message = "Not Connected";
    }
    await tick();
  }

  function handleProcessingModeChange() {
    if (processingMode === "Settings") {
      fetchDeviceInfo();
    }
  }

  async function fetchDeviceInfo() {
    try {
      console.log("Fetching SD card info...");
      const response = await fetch("http://192.168.4.1/sdinfo");
      if (!response.ok) throw new Error("Failed to fetch SD info");

      const data = await response.json();
      sdStatus = data.sd_status;
      totalSize = data.total_size.toFixed(2) + " MB";
      usedSize = data.used_size.toFixed(2) + " MB";
      freeSize = data.free_size.toFixed(2) + " MB";
      upTime = data.esp32_uptime_sec + " Seconds";
    } catch (error) {
      console.error("Error fetching SD info:", error);
      sdStatus = "Fail";
      totalSize = "Fail";
      usedSize = "Fail";
      freeSize = "Fail";
      upTime = "Fail";
    }
    await tick();
  }

  async function fetchDays() {
    try {
      console.log("Fetching available days...");
      const response = await fetch("http://192.168.4.1/days");
      if (!response.ok) throw new Error("Failed to fetch days");
      days = await response.json();
      selectedDay = days.length ? days[0] : "";
      fetchDrives();
    } catch (error) {
      console.error("Fetch error:", error);
      days = [];
      selectedDay = "";
    }
    await tick();
  }

  async function fetchDrives() {
    if (!selectedDay) return;
    try {
      console.log(`Fetching drives for ${selectedDay}...`);
      const response = await fetch(`http://192.168.4.1/drives?day=${selectedDay}`);
      if (!response.ok) throw new Error("Failed to fetch drives");
      drives = await response.json();
      selectedDrive = drives.length ? drives[0] : "";
    } catch (error) {
      console.error("Fetch error:", error);
      drives = [];
      selectedDrive = "";
    }
    await tick();
  }

  async function loadDrive() {
    if (!selectedDay || !selectedDrive) return;
    try {
      console.log(`Loading drive ${selectedDrive} for day ${selectedDay}...`);
      const response = await fetch(`http://192.168.4.1/drive?day=${selectedDay}&drive=${selectedDrive}`);
      if (!response.ok) throw new Error("Failed to load drive");

      const text = await response.text();
      const driveData = text
          .split("\n")
          .filter(line => line.trim() !== "")
          .map(line => JSON.parse(line));
      
      routeData = driveData;
      await tick();

      if (routeData.length > 0) {
        map.setView([routeData[0].gps.latitude, routeData[0].gps.longitude], 14);
      }

      updateRouteLine();
      updateMarkers();
      updateCharts();
    } catch (error) {
      console.error("Fetch error:", error);
    }
  }

  async function fetchLiveData() {
    try {
      console.log("Fetching live data...");
      const response = await fetch("http://192.168.4.1/live");
      if (!response.ok) throw new Error("Failed to fetch live data");
      const text = await response.text();
      const liveData = text
          .split("\n")
          .filter(line => line.trim() !== "")
          .map(line => JSON.parse(line));

      if (liveData.length > 0) {
        // Append new data if it’s new
        if (
          routeData.length === 0 ||
          routeData[routeData.length - 1].gps.time !== liveData[0].gps.time
        ) {
          routeData = [...routeData, ...liveData];
          const latestPoint = liveData[liveData.length - 1].gps;
          map.setView([latestPoint.latitude, latestPoint.longitude], 16, { animate: true });
        } else {
          console.log("Duplicate data");
        }
      }

      await tick();

      if (routeData.length > 0) {
        const latest = routeData[routeData.length - 1];
        liveTime = latest.gps.time;
        if (speedMetric == "kph") liveSpeed = latest.obd.speed;
        if (speedMetric == "mph") liveSpeed = (latest.obd.speed * 0.621371).toFixed(2);
        liveRPM = latest.obd.rpm;
        liveInstantMPG = latest.obd.instant_mpg.toFixed(2);
        liveAvgMPG = latest.obd.avg_mpg.toFixed(2);  
        liveThrottle = latest.obd.throttle;
        liveAccelX = (latest.imu.accel_x * 0.001 * 9.81).toFixed(2);
        liveAccelY = (latest.imu.accel_y * 0.001 * 9.81).toFixed(2);
      }

      updateRouteLine();
      updateMarkers();
      updateCharts();
    } catch (error) {
      console.error("Error fetching live data:", error);
    }
  }

  // Delete the entire day folder (all drives for the selected day)
  async function deleteDayFolder() {
    if (!selectedDay) return;
    const userConfirmed = await confirm(
      `Are you sure you want to delete all drives for ${selectedDay}?`,
      { title: "Confirm Delete" }
    );
    if (!userConfirmed) return;

    try {
      console.log(`Deleting day folder: /${selectedDay}`);
      const response = await fetch(`http://192.168.4.1/delete?path=/${selectedDay}`, {
        method: "DELETE"
      });
      if (!response.ok) throw new Error("Failed to delete day folder");
      console.log("All drives for the day have been deleted successfully.");
      await fetchDays();
      await fetchDrives();
    } catch (error) {
      console.error("Error deleting day folder:", error);
    }
  }

  // Delete only the selected drive file within the selected day folder.
  async function deleteDriveFile() {
    if (!selectedDrive) {
      console.log("No drive selected for deletion.");
      return;
    }
    const userConfirmed = await confirm(
      `Are you sure you want to delete the drive file ${selectedDrive}?`,
      { title: "Confirm Delete" }
    );
    if (!userConfirmed) return;

    try {
      const path = `/${selectedDay}/${selectedDrive}`;
      console.log(`Deleting drive file: ${path}`);
      const response = await fetch(
        `http://192.168.4.1/delete?path=${encodeURIComponent(path)}`,
        { method: "DELETE" }
      );
      if (!response.ok) throw new Error("Failed to delete drive file");
      console.log("The drive file has been deleted successfully.");
      await fetchDrives();
    } catch (error) {
      console.error("Error deleting drive file:", error);
    }
  }

  // Initialise the map 
  onMount(() => {
    map = L.map("map").setView([0, 0], 2);
    L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
      attribution:
        '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
    }).addTo(map);
  });



  // Whenever routeData changes, update map overlays and charts.
  $: if (routeData.length) {
    updateRouteLine();
    updateMarkers();
    updateCharts();
  }

  // Manage live polling based on processing mode.
  $: {
    if (processingMode === "real-time") {
      routeData = [];
      updateRouteLine();
      updateMarkers();

      if (!liveDataInterval) {
        checkConnection();
        fetchLiveData();
        liveDataInterval = setInterval(fetchLiveData, 2000);
      }
    } else {
      if (liveDataInterval) {
        clearInterval(liveDataInterval);
        liveDataInterval = null;
      }
    }
  }

  // Chart creation helper.
  const createChart = (canvas, label, data, color, xLabel, yLabel, labels) => {
    return new Chart(canvas, {
      type: "line",
      data: {
        labels,
        datasets: [{ label, data, borderColor: color, fill: false }]
      },
      options: {
        animation: false,
        responsive: true,
        plugins: { legend: { display: true } },
        scales: {
          x: { title: { display: true, text: xLabel }, ticks: { maxRotation: 45 } },
          y: { title: { display: true, text: yLabel } }
        }
      }
    });
  };

  async function updateCharts() {
    // Wait for DOM to update so that the canvases are available
    await tick();

    const timestamps = routeData.map((entry) => entry.gps.time);
    let speeds;
    if (speedMetric == "kph") {
      speeds = routeData.map((entry) => entry.obd.speed);
    } else {
      speeds = routeData.map((entry) => entry.obd.speed * 0.621371);
    }
    const rpms = routeData.map((entry) => entry.obd.rpm);
    const instantMpg = routeData.map((entry) => entry.obd.instant_mpg);
    const avgMpg = routeData.map((entry) => entry.obd.avg_mpg);
    const throttlePositions = routeData.map((entry) => entry.obd.throttle);
    const mafValues = routeData.map((entry) => entry.obd.maf);
    const accelX = routeData.map((entry) => entry.imu.accel_x * 0.001 * 9.81);
    const accelY = routeData.map((entry) => entry.imu.accel_y * 0.001 * 9.81);

    if (chart1) chart1.destroy();
    if (chart2) chart2.destroy();
    if (chart3) chart3.destroy();
    if (chart4) chart4.destroy();
    if (chart5) chart5.destroy();
    if (chart6) chart6.destroy();
    if (chart7) chart7.destroy();
    if (chart8) chart8.destroy();

    if (speedMetric == "kph") {
      chart1 = createChart(chart1Canvas, "Speed (kph)", speeds, "orange", "Time", "Speed (kph)", timestamps);
    } else {
      chart1 = createChart(chart1Canvas, "Speed (mph)", speeds, "orange", "Time", "Speed (mph)", timestamps);
    }
    chart2 = createChart(chart2Canvas, "RPM", rpms, "red", "Time", "RPM", timestamps);
    chart3 = createChart(chart3Canvas, "Instant MPG", instantMpg, "green", "Time", "MPG", timestamps);
    chart4 = createChart(chart4Canvas, "Average MPG", avgMpg, "purple", "Time", "MPG", timestamps);
    chart5 = createChart(chart5Canvas, "Throttle Position", throttlePositions, "brown", "Time", "Throttle (%)", timestamps);
    chart6 = createChart(chart6Canvas, "Accel X", accelX, "magenta", "Time", "Acceleration (m/s²)", timestamps);
    chart7 = createChart(chart7Canvas, "Accel Y", accelY, "darkblue", "Time", "Acceleration (m/s²)", timestamps);
    chart8 = createChart(chart8Canvas, "MAF", mafValues, "cyan", "Time", "MAF (g/s)", timestamps);
  }

  function updateMarkers() {
    markers.forEach((marker) => marker.remove());
    markers = [];

    if (showPoints) {
      routeData.forEach((point) => {
        const { latitude, longitude, time } = point.gps;
        const { speed, rpm, instant_mpg, throttle, avg_mpg } = point.obd;
        const marker = L.marker([latitude, longitude]).addTo(map);
        if (speedMetric == "mph") {
            marker.bindPopup(`
            <b>Time:</b> ${time}<br>
            <b>Speed:</b> ${(speed * 0.621371).toFixed(2)} mph<br>
            <b>RPM:</b> ${rpm}<br>
            <b>Instant MPG:</b> ${instant_mpg.toFixed(2)}<br>
            <b>Average MPG:</b> ${avg_mpg.toFixed(2)}<br>
            <b>Throttle:</b> ${throttle}%<br>
            <b>Cornerning:</b> ${(point.imu.accel_y * 0.001 * 9.81).toFixed(2)} m/s²<br>
            <b>Acceleration:</b> ${(point.imu.accel_x * 0.001 * 9.81).toFixed(2)} m/s²
            `);
        } else {
          marker.bindPopup(`
            <b>Time:</b> ${time}<br>
            <b>Speed:</b> ${speed} km/h<br>
            <b>RPM:</b> ${rpm}<br>
            <b>Instant MPG:</b> ${instant_mpg.toFixed(2)}<br>
            <b>Average MPG:</b> ${avg_mpg.toFixed(2)}<br>
            <b>Throttle:</b> ${throttle}%
            <b>Cornerning:</b> ${(point.imu.accel_y * 0.001 * 9.81).toFixed(2)} m/s²<br>
            <b>Acceleration:</b> ${(point.imu.accel_x * 0.001 * 9.81).toFixed(2)} m/s²
          `);
        }
        markers.push(marker);
      });
    }
  }

  function updateRouteLine() {
    map.eachLayer((layer) => {
      if (layer instanceof L.Polyline) {
        map.removeLayer(layer);
      }
    });

    const route = routeData.map((point) => [point.gps.latitude, point.gps.longitude]);
    if (route.length < 2) return;

    let metricValues;
    if (selectedMetric === "accel_x" || selectedMetric === "accel_y") {
      metricValues = routeData.map((point) => point.imu[selectedMetric] * 0.001 * 9.81);
    } else if (selectedMetric === "default") {
      metricValues = routeData.map(() => 1);
    } else {
      metricValues = routeData.map((point) => point.obd[selectedMetric]);
    }

    const maxMetric = Math.max(...metricValues);
    const minMetric = Math.min(...metricValues);
    const range = maxMetric - minMetric || 1;

    for (let i = 0; i < route.length - 1; i++) {
      const start = route[i];
      const end = route[i + 1];
      const normalizedValue = (metricValues[i] - minMetric) / range;
      const color = `rgb(${255 * normalizedValue}, 0, ${255 * (1 - normalizedValue)})`;
      L.polyline([start, end], { color, weight: 5 }).addTo(map);
    }
  }

  function togglePoints() {
    showPoints = !showPoints;
    updateMarkers();
  }

  // Initial connection check.
  checkConnection();
</script>

<main>
  <h1>Driving Analysis Interface</h1>
  <div id="map-container">
    <div id="control-panel">
      {#if message != "Connected"}
        <div id="connection-status-container" style="background-color: #f8d7da; color: #721c24;">
          Status: {message}
          <button on:click={checkConnection}>Retry</button>
        </div>
      {:else}
        <div id="connection-status-container" style="background-color: #d4edda; color: #155724;">
          Status: Connected
          <button on:click={checkConnection}>Retry</button>
        </div>
      {/if}

      <label for="processing-mode">Mode:</label>
      <select class="dropdown" bind:value={processingMode} on:change={handleProcessingModeChange}>
        <option value="post">Post</option>
        <option value="real-time">Real-time</option>
        <option value="Settings">Settings</option>
      </select>
      
      {#if processingMode === "post"}
        <label for="day-select">Select Day:</label>
        <select class="dropdown" bind:value={selectedDay} on:change={fetchDrives}>
          {#each days as day}
            <option value={day}>{day}</option>
          {/each}
        </select>

        <label for="drive-select">Select Drive:</label>
        <select class="dropdown" bind:value={selectedDrive}>
          {#each drives as drive}
            <option value={drive}>{drive}</option>
          {/each}
        </select>

        <button on:click={loadDrive}>Load Drive</button>

        <div class="div-container">
          <fieldset>
            <legend>Route Data Overlay:</legend>
            {#each [
              { value: "default", label: "Default" },
              { value: "speed", label: "Speed" },
              { value: "instant_mpg", label: "Instant MPG" },
              { value: "avg_mpg", label: "Average MPG" },
              { value: "throttle", label: "Throttle" },
              { value: "accel_x", label: "Acceleration" },
              { value: "accel_y", label: "Cornerning" },
              { value: "rpm", label: "RPM" }
            ] as option}
              <label class="radio-option">
                <input 
                  type="radio" 
                  name="data-overlay" 
                  bind:group={selectedMetric} 
                  value={option.value} 
                  on:change={updateRouteLine}
                />
                {option.label}
              </label>
            {/each}
          </fieldset>
        </div>
        <button on:click={togglePoints}>
          {showPoints ? "Hide Points" : "Show Points"}
        </button>
      {:else if processingMode === "real-time"}
        <label for="data-overlay">Route Data Overlay:</label>
        <select class="dropdown" name="data-overlay" bind:value={selectedMetric} on:change={updateRouteLine}>
          <option value="default">Default</option>
          <option value="speed">Speed</option>
          <option value="instant_mpg">Instant MPG</option>
          <option value="throttle">Throttle Position</option>
          <option value="accel_x">Accel X</option>
          <option value="accel_y">Accel Y</option>
          <option value="rpm">RPM</option>
          <option value="avg_mpg">Average MPG</option>
        </select>

        <div class="div-container" id="live-data-container">
          <p class="live-data">Time: {liveTime}</p>
          <p class="live-data">Speed: {liveSpeed}</p>
          <p class="live-data">RPM: {liveRPM}</p>
          <p class="live-data">Instant MPG: {liveInstantMPG}</p>
          <p class="live-data">Average MPG: {liveAvgMPG}</p>  
          <p class="live-data">Throttle: {liveThrottle}</p>
          <p class="live-data">Accel X: {liveAccelX}</p>
          <p class="live-data">Accel Y: {liveAccelY}</p>
        </div>
        <button on:click={togglePoints}>
          {showPoints ? "Hide Points" : "Show Points"}
        </button>
      {:else if processingMode === "Settings"}
        <label for="speed-metric">Speed metric: </label>
        <select name="speed-metric" class="dropdown" bind:value={speedMetric}>
          <option value="mph">MPH</option>
          <option value="kph">KPH</option>
        </select>
        <label for="sdinfo">SD card details: </label>
        <div class="sd-info-box">
          <p><b>SD Card Status:</b> {sdStatus}</p>
          <p><b>Total Size:</b> {totalSize}</p>
          <p><b>Used Size:</b> {usedSize}</p>
          <p><b>Free Size:</b> {freeSize}</p>
          <p><b>Up-time:</b> {upTime}</p>
        </div>

        <div class="delete-management">
          <div>
            <label for="delete-day-select">Select Day:</label>
            <select id="delete-day-select" class="dropdown" bind:value={selectedDay} on:change={fetchDrives}>
              {#each days as day}
                <option value={day}>{day}</option>
              {/each}
            </select>
          </div>
          <div>
            <label for="delete-drive-select">Select Drive:</label>
            <select id="delete-drive-select" class="dropdown" bind:value={selectedDrive}>
              {#each drives as drive}
                <option value={drive}>{drive}</option>
              {/each}
            </select>
          </div>
          <div id="delete-buttons-container">
            <button on:click={deleteDayFolder} class="delete-buttons">
              Delete All Drives for {selectedDay}
            </button>
            <button on:click={deleteDriveFile} class="delete-buttons">
              Delete Selected Drive on {selectedDrive}
            </button>
          </div>
          <button on:click={loadDrive}>Load Drive</button>
        </div>
      {/if}
    </div>

    <div id="map"></div>
  </div>

  {#if routeData.length !== 0 && processingMode === "post"}
    <div id="graphs-container">
      <div class="chart"><canvas bind:this={chart1Canvas} id="chart1"></canvas></div>
      <div class="chart"><canvas bind:this={chart2Canvas} id="chart2"></canvas></div>
      <div class="chart"><canvas bind:this={chart3Canvas} id="chart3"></canvas></div>
      <div class="chart"><canvas bind:this={chart4Canvas} id="chart4"></canvas></div>
      <div class="chart"><canvas bind:this={chart5Canvas} id="chart5"></canvas></div>
      <div class="chart"><canvas bind:this={chart6Canvas} id="chart6"></canvas></div>
      <div class="chart"><canvas bind:this={chart7Canvas} id="chart7"></canvas></div>
      <div class="chart"><canvas bind:this={chart8Canvas} id="chart8"></canvas></div>
    </div>
  {/if}
</main>
