#ifndef WEBSERVERSTYLE_H
#define WEBSERVERSTYLE_H
#include "mqttConfig.h"



const char MAIN_PAGE_HEADER[] = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Cat Treat Dispenser</title>
)";

const char AP_CONFIG_PAGE_HEADER[] = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Connecting...</title>
)";


const char COMMON_HEADER[] = R"(
        <style>
            body {
                font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                background-color: #f5f5f5;
                color: #333;
                line-height: 1.6;
                margin: 0;
                padding: 0;
            }
            .container {
                max-width: 600px;
                margin: 30px auto;
                padding: 25px;
                background: white;
                border-radius: 10px;
                box-shadow: 0 4px 15px rgba(0,0,0,0.1);
            }
            h1 {
                color: #2c3e50;
                text-align: center;
                margin-bottom: 25px;
            }
            .form-group {
                margin-bottom: 20px;
            }
            label {
                display: block;
                margin-bottom: 8px;
                font-weight: 600;
            }
            input[type="text"],
            input[type="password"],
            input[type="number"],
            select {
                width: 100%;
                padding: 10px;
                border: 1px solid #ddd;
                border-radius: 4px;
                font-size: 16px;
                box-sizing: border-box;
            }
            button, input[type="submit"] {
                background-color: #3498db;
                color: white;
                border: none;
                padding: 12px 20px;
                border-radius: 4px;
                cursor: pointer;
                font-size: 16px;
                width: 100%;
                transition: background-color 0.3s;
                margin-top: 5px;
            }
            button:hover, input[type="submit"]:hover {
                background-color: #2980b9;
            }
            .btn-primary {
                background-color: #3498db;
            }
            .btn-success {
                background-color: #2ecc71;
            }
            .btn-danger {
                background-color: #e74c3c;
            }
            .btn-warning {
                background-color: #f39c12;
            }
            .network-list {
                margin-top: 20px;
                border: 1px solid #eee;
                border-radius: 4px;
                max-height: 200px;
                overflow-y: auto;
            }
            .network-item {
                padding: 10px;
                border-bottom: 1px solid #eee;
                cursor: pointer;
                transition: background-color 0.2s;
                display: flex;
                justify-content: space-between;
                align-items: center;
            }
            .network-item:hover {
                background-color: #f8f9fa;
            }
            .network-info {
                flex: 1;
                white-space: nowrap;
                overflow: hidden;
                text-overflow: ellipsis;
            }
            .network-stats {
                display: flex;
                align-items: center;
                margin-left: 10px;
            }
            .rssi {
                font-weight: bold;
                margin-right: 10px;
                min-width: 40px;
                text-align: right;
            }
            .rssi.excellent { color: #2ecc71; } /* > -60 dBm */
            .rssi.good { color: #f39c12; }     /* -60 to -68 dBm */
            .rssi.fair { color: #e67e22; }     /* -68 to -75 dBm */
            .rssi.weak { color: #e74c3c; }      /* < -75 dBm */
            .encryption {
                font-size: 12px;
                color: #2ecc71;
                font-weight: bold;
                min-width: 50px;
                text-align: right;
            }
            .open-network .encryption {
                color: #e74c3c;
            }
            .loading {
                text-align: center;
                padding: 20px;
                color: #7f8c8d;
            }
            .hidden {
                display: none;
            }
            .status-message {
                padding: 15px;
                margin: 15px 0;
                border-radius: 4px;
                text-align: center;
            }
            .status-success {
                background-color: #d4edda;
                color: #155724;
            }
            .status-error {
                background-color: #f8d7da;
                color: #721c24;
            }
            .status-info {
                background-color: #d1ecf1;
                color: #0c5460;
            }
            .two-column {
                display: flex;
                gap: 15px;
            }
            .column {
                flex: 1;
            }
        </style>
    </head>
    <body>
        <div class="container">
    )";

const char COMMON_FOOTER[] = R"(
        </div>
    </body>
    </html>
    )";


const char AP_CONFIG_PAGE[] = R"(
        <h1>WiFi Configuration</h1>
        
        <button id="scanBtn" class="btn-success">Scan Networks</button>
        
        <div id="networkList" class="network-list hidden">
            <div id="loading" class="loading">Scanning for networks...</div>
            <div id="networks"></div>
        </div>
        
        <form action="/connect" method="get">
            <div class="form-group">
                <label for="ssid">Network Name (SSID)</label>
                <input type="text" id="ssid" name="ssid" required>
            </div>
            
            <div class="form-group">
                <label for="pass">Password</label>
                <input type="password" id="pass" name="pass">
            </div>
            
            <input type="submit" class="btn-primary" value="Connect">
        </form>

        <form action"/trial" method="get">
            <input type="submit" class="btn-warning" value="Trial Mode [demo webUI without connecting wifi]">
        </form>
        
        <script>
            document.getElementById('scanBtn').addEventListener('click', function() {
                var networkList = document.getElementById('networkList');
                var loading = document.getElementById('loading');
                var networksDiv = document.getElementById('networks');
                
                networkList.classList.remove('hidden');
                loading.classList.remove('hidden');
                networksDiv.innerHTML = '';
                
                fetch('/scan')
                    .then(response => response.json())
                    .then(data => {
                        loading.classList.add('hidden');
                        
                        if (data.length === 0) {
                            networksDiv.innerHTML = '<div class="network-item">No networks found</div>';
                            return;
                        }
                        
                        data.forEach(network => {
                            const networkItem = document.createElement('div');
                            networkItem.className = 'network-item';
                            if (network.encryption === 'Open') {
                                networkItem.classList.add('open-network');
                            }
                            
                            // Determine RSSI color class
                            let rssiClass = 'weak';
                            if (network.rssi > -60) rssiClass = 'excellent';
                            else if (network.rssi > -68) rssiClass = 'good';
                            else if (network.rssi > -75) rssiClass = 'fair';
                            
                            networkItem.innerHTML = `
                                <div class="network-info">${network.ssid}</div>
                                <div class="network-stats">
                                    <div class="rssi ${rssiClass}">${network.rssi} dBm</div>
                                    <div class="encryption">${network.encryption}</div>
                                </div>
                            `;
                            
                            networkItem.addEventListener('click', function() {
                                document.getElementById('ssid').value = network.ssid;
                                document.getElementById('pass').focus();
                            });
                            
                            networksDiv.appendChild(networkItem);
                        });
                    })
                    .catch(error => {
                        loading.classList.add('hidden');
                        networksDiv.innerHTML = '<div class="network-item">Error scanning networks</div>';
                        console.error('Error:', error);
                    });
            });
        </script>
)";




String build_main_page_body(bool mqtt_conn_status, int hallEffectCount, int hallEffectRunDistanceMultiplier, int distanceThreshold, int totalDistance, int totalTreatsDispensed, bool outOfTreats, String mqttserver, int mqttport, String mqttuser, String mqttpass, String mqttprefix, bool mqttenabled)
{

    return String(
R"(<!DOCTYPE html>
    <html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Cat Treat Dispenser</title>

        <style>
            body {
                font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                background-color: #f5f5f5;
                color: #333;
                line-height: 1.6;
                margin: 0;
                padding: 0;
            }
            .container {
                max-width: 600px;
                margin: 30px auto;
                padding: 25px;
                background: white;
                border-radius: 10px;
                box-shadow: 0 4px 15px rgba(0,0,0,0.1);
            }
            h1 {
                color: #2c3e50;
                text-align: center;
                margin-bottom: 25px;
            }
            .form-group {
                margin-bottom: 20px;
            }
            label {
                display: block;
                margin-bottom: 8px;
                font-weight: 600;
            }
            input[type="text"],
            input[type="password"],
            input[type="number"],
            select {
                width: 100%;
                padding: 10px;
                border: 1px solid #ddd;
                border-radius: 4px;
                font-size: 16px;
                box-sizing: border-box;
            }
            button, input[type="submit"] {
                
                color: white;
                border: none;
                padding: 12px 20px;
                border-radius: 4px;
                cursor: pointer;
                font-size: 16px;
                width: 100%;
                transition: background-color 0.3s;
                margin-top: 5px;
            }
            button:hover, input[type="submit"]:hover {
                background-color: #2980b9;
            }
            .btn-primary {
                background-color: #3498db;
            }
            .btn-success {
                background-color: #2ecc71;
            }
            .btn-danger {
                background-color: #e74c3c;
            }
            .btn-warning {
                background-color: #f39c12;
            }
            }
            .hidden {
                display: none;
            }
            .two-column {
                display: flex;
                gap: 15px;
            }
            .column {
                flex: 1;
            }
            .toggle-switch {
                position: relative;
                display: inline-block;
                width: 60px;
                height: 34px;
                vertical-align: middle;
            }
            .toggle-switch input {
                opacity: 0;
                width: 0;
                height: 0;
            }
            .slider {
                position: absolute;
                cursor: pointer;
                top: 0;
                left: 0;
                right: 0;
                bottom: 0;
                background-color: #ccc;
                transition: .4s;
                border-radius: 34px;
            }
            .slider:before {
                position: absolute;
                content: "";
                height: 26px;
                width: 26px;
                left: 4px;
                bottom: 4px;
                background-color: white;
                transition: .4s;
                border-radius: 50%;
            }
            input:checked + .slider {
                background-color: #2ecc71;
            }
            input:checked + .slider:before {
                transform: translateX(26px);
            }
        </style>
    </head>
    <body>
        <div class="container">

            <h1>Cat Treat Dispenser</h1>
            <!-- Status Section at Top -->
            <div class="status-message status-info" style="margin-bottom: 20px;">
                <div style="display: flex; justify-content: space-between; margin-bottom: 8px;">
                    <span>Current Progress:</span>
                    <span><strong>)") + String((hallEffectCount * hallEffectRunDistanceMultiplier)/100) + "/" + String(distanceThreshold / 100) + R"( Meters</strong></span>
                </div>
                <div style="display: flex; justify-content: space-between; margin-bottom: 8px;">
                    <span>Total Distance:</span>
                    <span><strong>)" + String(totalDistance / 100) + R"( Meters</strong></span>
                </div>
                <div style="display: flex; justify-content: space-between; margin-bottom: 8px;">
                    <span>Treats Dispensed:</span>
                    <span><strong>)" + String(totalTreatsDispensed) + R"(</strong></span>
                </div>
                <div style="display: flex; justify-content: space-between; margin-bottom: 8px;">
                    <span>Out of Treats:</span>
                    <span><strong>)" + String(outOfTreats ? "<b style=\"color: #721c24;\">True</b>" : "False") + R"(</strong></span>
                </div>
                <div style="display: flex; justify-content: space-between;">
                    <span>MQTT Status:</span>
                    <span><strong>)" + String(mqtt_conn_status ? "True" : "False") + R"=(</strong></span>
                    </div>
                </div>
    
                <!-- Quick Actions Section -->
                <h2>Quick Actions</h2>
                <div style="display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-bottom: 20px;">
                    <form action="/dispenseTreat" method="post">
                        <input type="submit" class="btn-success" value="Dispense Treat">
                    </form>
                    <form action="/resetErrorStates" method="post">
                        <input type="submit" class="btn-warning" value="Reset Errors">
                    </form>
                    <form action="/restart" method="post" onsubmit="return confirmRestart()">
                        <input type="submit" class="btn-warning" value="Restart MCU">
                    </form>
                </div>
    
                <!-- Distance Threshold -->
                <h2>Settings</h2>
                <form action="/settings" method="post">
                    <div class="form-group">
                        <label for="distanceThreshold">Distance Threshold (Meters)</label>
                        <input type="number" id="distanceThreshold" name="distanceThreshold" value=")=" + String(distanceThreshold / 100) + R"(" required>
                    </div>

                    <!-- Collapsible MQTT Section -->
                    <details style="margin: 20px 0;">
                        <summary style="font-size: 1.2em; font-weight: bold; cursor: pointer; padding: 5px 0;">MQTT Settings</summary>
    
                        <div style="margin-top: 15px;">

                        <!-- MQTT Toggle Section -->
                        <div class="form-group" style="margin: 20px 0;">
                            <label for="mqttEnabled" style="display: inline-block; margin-left: 10px; vertical-align: middle;">
                                Enable MQTT
                                </span>
                            </label>
                            <label class="toggle-switch">
                                <input type="checkbox" id="mqttEnabled" name="mqttEnabled" )" + (mqttenabled ? "checked" : "") + R"(>
                                <span class="slider round"></span>
                            </label>
                        </div>

                            <div class="form-group">
                                <label for="mqttServer">MQTT Server</label>
                                <input type="text" id="mqttServer" name="mqttServer" value=")" + mqttserver + R"(">
                            </div>
    
                            <div class="form-group">
                                <label for="mqttPort">MQTT Port</label>
                                <input type="number" id="mqttPort" name="mqttPort" value=")" + String(mqttport) + R"(">
                            </div>
    
                            <div class="form-group">
                                <label for="mqttUsername">MQTT Username</label>
                                <input type="text" id="mqttUsername" name="mqttUsername" value=")" + mqttuser + R"=(">
                            </div>
    
                            <div class="form-group" style="position: relative;">
                                <label for="mqttPassword">MQTT Password</label>
                                <input type="password" id="mqttPassword" name="mqttPassword" value=")=" + mqttpass + R"=(">
                                <button type="button" onclick="togglePassword()" 
                                        style="position: absolute; right: 0; top: 50%; transform: translateY(50%);
                                               background: none; border: none; color: #3498db; cursor: pointer;">
                                    Show
                                </button>
                            </div>
    
                            <div class="form-group">
                                <label for="mqttTopicPrefix">MQTT Topic Prefix</label>
                                <input type="text" id="mqttTopicPrefix" name="mqttTopicPrefix" value=")=" + mqttprefix + R"==(">
                            </div>
                        </div>
                    </details>
    
                    <input type="submit" class="btn-primary" value="Save Settings">
                </form>
    
                <!-- System Tools Section -->
                <h2 style="margin-top: 30px;">System Tools</h2>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                    <form action="/resetStats" method="post" onsubmit="return confirmAction('Are you sure you want to reset all statistics?')">
                        <input type="submit" class="btn-danger" value="Reset Stats">
                    </form>
                    <form action="/reset_config" method="get" onsubmit="return confirmAction('Are you sure you want to clear MQTT / system configuration?')">
                        <input type="submit" class="btn-danger" value="Clear Config">
                    </form>
                    <form action="/reset_wifi" method="get" onsubmit="return confirmAction('Are you sure you want to clear WiFi configuration?')">
                        <input type="submit" class="btn-warning" value="Clear WiFi">
                    </form>
                </div>
    
                <script>
                    function togglePassword() {
                        const passwordField = document.getElementById('mqttPassword');
                        const toggleButton = event.target;
    
                        if (passwordField.type === 'password') {
                            passwordField.type = 'text';
                            toggleButton.textContent = 'Hide';
                        } else {
                            passwordField.type = 'password';
                            toggleButton.textContent = 'Show';
                        }
                    }
    
                    function confirmAction(message) {
                        return confirm(message);
                    }
    
                    function confirmRestart() {
                        return confirm('Are you sure you want to restart the device?');
                    }
                </script>
    
            </div>
        </body>
        </html>
        )==";
}

#endif