
const char* info_html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>Klaussometer Sensor Info</title>
  <style>
    body {
      background-color: #f0f2f5;
      font-family: Arial, sans-serif;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      margin: 0;
      color: #333;
    }
    .container {
      background-color: #fff;
      padding: 30px;
      border-radius: 10px;
      box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
      text-align: center;
      width: 90%;
      max-width: 400px;
    }
    h1 {
      color: #007bff;
      margin-bottom: 20px;
    }
    p {
        color: #555;
        font-size: 14px;
        text-align: left;
        margin: 5px 0;
    }
    .section-title {
        font-weight: bold;
        color: #007bff;
        margin-top: 20px;
    }
    .link-button {
        display: inline-block;
        background-color: #007bff;
        color: #fff;
        border: none;
        padding: 12px 24px;
        border-radius: 5px;
        cursor: pointer;
        font-size: 16px;
        transition: background-color 0.3s ease;
        text-decoration: none;
        margin-top: 20px;
    }
    .link-button:hover {
      background-color: #0056b3;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Klaussometer Sensor Info</h1>
    {{content}}
    <a href="/update" class="link-button">Update Firmware</a>
    <script>
        // Function to fetch and update data
        function updateData() {
            // Use the XMLHttpRequest object to make an AJAX request
            var xhttp = new XMLHttpRequest();
            xhttp.onreadystatechange = function() {
                // Check if the request is complete and successful
                if (this.readyState == 4 && this.status == 200) {
                    // Parse the JSON response
                    var data = JSON.parse(this.responseText);

                    // Update the HTML elements with the new data
                    document.getElementById('time').innerHTML = data.time;
                    document.getElementById('temp').innerHTML = data.temperature;
                    document.getElementById('humid').innerHTML = data.humidity;
                    // Update voltage only if the element exists (for mains-powered devices)
                    var voltageElement = document.getElementById('voltage');
                    if (voltageElement) {
                        voltageElement.innerHTML = data.voltage;
                    }
                    document.getElementById('uptime').innerHTML = data.uptime;
                }
            };
            // Open a GET request to the /data endpoint
            xhttp.open("GET", "/data", true);
            // Send the request
            xhttp.send();
        }

        // Call the function once when the page loads
        window.onload = function() {
            updateData();
        };

        // Set up a timer to call updateData() every 5 seconds (5000 milliseconds)
        setInterval(updateData, 5000);
    </script>
  </div>
</body>
</html>
)=====";

const char* ota_html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>Klaussometer Sensor OTA Update</title>
  <style>
    body {
      background-color: #f0f2f5;
      font-family: Arial, sans-serif;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      margin: 0;
      color: #333;
    }
    .container {
      background-color: #fff;
      padding: 30px;
      border-radius: 10px;
      box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
      text-align: center;
      width: 90%;
      max-width: 400px;
    }
    h1 {
      color: #007bff;
      margin-bottom: 20px;
    }
    p {
        color: #555;
        font-size: 14px;
    }
    form {
      margin-top: 20px;
    }
    input[type="file"] {
      border: 2px dashed #ccc;
      padding: 20px;
      border-radius: 5px;
      width: calc(100% - 40px);
      margin-bottom: 20px;
    }
    input[type="submit"] {
      background-color: #007bff;
      color: #fff;
      border: none;
      padding: 12px 24px;
      border-radius: 5px;
      cursor: pointer;
      font-size: 16px;
      transition: background-color 0.3s ease;
    }
    input[type="submit"]:hover {
      background-color: #0056b3;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Klaussometer Sensor OTA Update</h1>
    <p>Current Firmware Version: {{FIRMWARE_VERSION}}</p>
    <form method="POST" action="/update" enctype="multipart/form-data">
      <input type="file" name="firmware" id="firmware" accept=".bin">
      <input type="submit" value="Update Firmware">
    </form>
  </div>
</body>
</html>
)=====";