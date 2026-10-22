"use strict";

// ========================================================================
// Bluetooth link: connect, chunked serialized sending, line-buffered receive.
// ========================================================================

// ---------------------------------------------------------------------------
// Bluetooth
// ---------------------------------------------------------------------------
function setConnectedState(isConnected) {
  statusDot.className = isConnected ? "status-dot on" : "status-dot";
  statusText.textContent = isConnected
    ? "Connected to " + (bluetoothDevice ? bluetoothDevice.name : "robot")
    : "Not connected";
  connectButton.textContent = isConnected ? "Disconnect" : "Connect";
  connectButton.className = isConnected ? "connected" : "";
}

function connectToRobot() {
  navigator.bluetooth.requestDevice({
    filters: [{ services: [BLE_SERVICE_UUID] }, { namePrefix: "Aqua" }, { namePrefix: "JDY" }],
    optionalServices: [BLE_SERVICE_UUID]
  }).then(function (device) {
    bluetoothDevice = device;
    device.addEventListener("gattserverdisconnected", handleDisconnect);
    logLine("Connecting to " + device.name + "...");
    return device.gatt.connect();
  }).then(function (server) {
    return server.getPrimaryService(BLE_SERVICE_UUID);
  }).then(function (service) {
    return service.getCharacteristic(BLE_CHARACTERISTIC_UUID);
  }).then(function (characteristic) {
    serialCharacteristic = characteristic;
    characteristic.addEventListener("characteristicvaluechanged", handleIncomingData);
    return characteristic.startNotifications();
  }).then(function () {
    setConnectedState(true);
    logLine("Connected. Notifications on.", "rx");
    sendCommand("H1\n");
  }).catch(function (error) {
    logLine("Connect failed: " + error.message, "err");
    setConnectedState(false);
  });
}

function disconnectFromRobot() {
  sendCommand("H0\n");
  setTimeout(function () {
    if (bluetoothDevice && bluetoothDevice.gatt.connected) {
      bluetoothDevice.gatt.disconnect();
    }
  }, 200);
}

function handleDisconnect() {
  serialCharacteristic = null;
  setConnectedState(false);
  logLine("Disconnected.", "err");
}

var incomingLineBuffer = "";

function handleIncomingData(event) {
  var decoder = new TextDecoder();
  incomingLineBuffer = incomingLineBuffer + decoder.decode(event.target.value);
  var newlineIndex = incomingLineBuffer.indexOf("\n");
  while (newlineIndex >= 0) {
    var completeLine = incomingLineBuffer.slice(0, newlineIndex).replace(/\r$/, "");
    incomingLineBuffer = incomingLineBuffer.slice(newlineIndex + 1);
    if (completeLine.length > 0) {
      if (completeLine.indexOf("T,") === 0) {
        updateTelemetry(completeLine);
      } else if (completeLine.indexOf("P,") === 0) {
        handleProgramStepEcho(completeLine);
      } else {
        logLine("< " + completeLine, "rx");
        if (typeof robotReplyHook === "function") {
          robotReplyHook(completeLine);
        }
      }
    }
    newlineIndex = incomingLineBuffer.indexOf("\n");
  }
}

connectButton.addEventListener("click", function () {
  if (serialCharacteristic) {
    disconnectFromRobot();
  } else {
    connectToRobot();
  }
});

// ---------------------------------------------------------------------------
// Sending — all writes serialized; long payloads chunked to the BLE MTU
// ---------------------------------------------------------------------------
function pauseBetweenChunks() {
  return new Promise(function (resolve) {
    setTimeout(resolve, BLE_CHUNK_GAP_MS);
  });
}

function sendStopInsurance() {
  // Stop twice: idempotent, and the second copy rides a different collision
  // window on the half-duplex link. A lost stop byte can't cause a runaway.
  sendCommand("SS");
}

function sendCommand(commandText) {
  if (!serialCharacteristic) {
    logLine("Not connected.", "err");
    return;
  }
  var encoder = new TextEncoder();
  var payload = encoder.encode(commandText);

  function writeOneChunk(chunk) {
    if (!serialCharacteristic) {
      return Promise.resolve();
    }
    if (serialCharacteristic.writeValueWithoutResponse) {
      return serialCharacteristic.writeValueWithoutResponse(chunk);
    }
    return serialCharacteristic.writeValue(chunk);
  }

  function writeChunkGuarded(chunk) {
    // A GATT write that never settles used to stall the whole queue —
    // later presses then flushed STALE commands (the spin button running
    // an old drive test). 1.5s and we move on, loudly.
    return Promise.race([
      writeOneChunk(chunk).then(function () { return "ok"; }),
      new Promise(function (resolve) { setTimeout(function () { resolve("timeout"); }, 1500); })
    ]).then(function (result) {
      if (result === "timeout") {
        logLine("BLE write timed out \u2014 skipping ahead", "err");
      }
    });
  }

  var offset;
  for (offset = 0; offset < payload.length; offset += BLE_CHUNK_SIZE) {
    (function (chunk, isLast) {
      writeChain = writeChain.then(function () {
        return writeChunkGuarded(chunk);
      }).then(function () {
        return isLast ? null : pauseBetweenChunks();
      }).catch(function (error) {
        logLine("Write failed: " + error.message, "err");
      });
    })(payload.slice(offset, offset + BLE_CHUNK_SIZE), offset + BLE_CHUNK_SIZE >= payload.length);
  }
}
