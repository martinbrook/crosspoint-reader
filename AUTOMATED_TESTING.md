# Automated Testing Infrastructure

## Context

Currently, the CrossPoint Reader project has no automated testing framework for the firmware. The CI/CD pipeline only performs builds, formatting checks, and static analysis. There is no mechanism to:

1. Connect to a physical device from GitHub Actions
2. Flash new firmware automatically
3. Execute test suites on the device
4. Capture and verify screenshot output

## Assumption: Self-Hosted GitHub Runners

We assume you'll use **self-hosted GitHub runners** - running GitHub Actions directly on machines that have your ESP32 devices attached via USB.

### Docker-Based Runner (Recommended)

Instead of installing the runner directly on your host, use a Docker container:

**Dockerfile** (`scripts/test_runner/Dockerfile.runner`):
```dockerfile
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    curl \
    wget \
    python3 \
    python3-pip \
    git \
    usbutils \
    && rm -rf /var/lib/apt/lists/*

# Install PlatformIO Core
RUN pip3 install -U https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip

# Create runner user
RUN useradd -m runner

# Download GitHub Actions Runner
WORKDIR /home/runner
RUN wget https://github.com/actions/runner/releases/download/v2.311.0/actions-runner-linux-x64-2.311.0.tar.gz \
    && tar xzf actions-runner-linux-x64-2.311.0.tar.gz \
    && rm actions-runner-linux-x64-2.311.0.tar.gz

# Install runner dependencies
RUN ./bin/installdependencies.sh

USER runner

# Mount your device directory (USB passthrough via --device)
CMD ["./run.sh"]
```

**Run command:**
```bash
docker run -d \
  --name github-runner \
  --device /dev/ttyACM0 \
  --device /dev/ttyUSB0 \
  -v /path/to/repo:/home/runner/repo \
  -w /home/runner \
  github-runner \
  ./config.sh --url https://github.com/your-username/crosspoint-reader --token YOUR_TOKEN \
  && ./run.sh
```

**Advantages:**
- Isolated environment - less risk to host system
- Easier to manage dependencies
- Can run multiple runners on same machine
- Easier to tear down and recreate

## Goals

- Enable GitHub Actions to run tests on physical ESP32 devices
- Capture screenshots for visual regression detection
- Allow extensible test suite development
- Minimal code changes to existing firmware

## Implementation Plan

### Phase 1: Core Device Changes

#### 1.1 TestActivity (`src/activities/tester/TestActivity.h` and `TestActivity.cpp`)

New activity that handles test mode execution:

```cpp
class TestActivity : public Activity {
  enum class State { IDLE, RUNNING_TESTS, TEST_COMPLETE };
  
  // Serial command handlers
  void handleTestModeCommand();
  void handleListTestsCommand();
  void handleRunTestCommand(const String& params);
  void handleButtonCommand(const String& params);
  void handleScreenshotCommand(const String& params);
  void handleExitCommand();
  
  // Test execution
  void runTestSuite(const String& suiteName);
  void simulateButtonPress(uint8_t button, uint32_t durationMs);
  void captureScreenshot(const String& name);
  void reportResult(const String& testName, bool passed, const String& details);
};
```

**Location**: `src/activities/tester/`

#### 1.2 Extend Serial Command Handler (`src/main.cpp`)

Add TestActivity integration to existing command handler (lines 328-341):

```cpp
if (line.startsWith("CMD:")) {
  String cmd = line.substring(4);
  cmd.trim();
  if (cmd == "SCREENSHOT") { /* existing */ }
  else if (cmd == "TEST_MODE") { activityManager.pushActivity(createTestActivity()); }
  else if (cmd.startsWith("TEST_")) { testActivity->handleTestCommand(cmd); }
  // ... other test commands
}
```

#### 1.3 Test Protocol Commands

| Command | Parameters | Response |
|---------|------------|----------|
| `TEST_MODE` | - | `TEST_MODE_ENTERED` |
| `TEST_EXIT` | - | `TEST_MODE_EXITED` |
| `LIST_TESTS` | - | `TEST_LIST:<json>` |
| `RUN_TEST` | `suite`, `name` | `TEST_RESULT:<json>` |
| `BUTTON_PRESS` | `button`, `duration` | `BUTTON_SENT` |
| `SCREENSHOT` | `name` | `SCREENSHOT_START:<size>`, data, `SCREENSHOT_END` |

### Phase 2: Test Suite Framework

#### 2.1 Test Runner (`scripts/test_runner/test_runner.py`)

Main orchestrator with classes:

- `SerialConnector` - Serial connection management
- `TestRunner` - Test orchestration
- `TestCase` - Base class for tests
- Built-in test suites:
  - `test_display.py` - Screen refresh, grayscale rendering
  - `test_button.py` - Button detection, long press
  - `test_font.py` - Text rendering, character display
  - `test_reader.py` - EPUB rendering, page navigation

#### 2.2 Test Results Format

```json
{
  "timestamp": "2026-04-08T12:00:00Z",
  "device": "X4",
  "firmware_version": "v2.5.0-abc123",
  "tests": [
    {"name": "display_refresh", "pass": true, "duration_ms": 150},
    {"name": "button_power", "pass": false, "duration_ms": 500, "error": "Button not detected"}
  ]
}
```

### Phase 3: CI/CD Integration

#### 3.1 GitHub Actions Workflow (`.github/workflows/test.yml`)

The workflow runs on `self-hosted` runners (your machines with devices attached):

```yaml
name: Device Testing

on:
  push:
    branches: [master]
  pull_request:
  workflow_dispatch:

jobs:
  test-device:
    # Runs on your self-hosted runner (machine with USB device attached)
    runs-on: self-hosted
    timeout-minutes: 30
    
    steps:
      - uses: actions/checkout@v6
        with:
          submodules: recursive
      
      - name: Install PlatformIO
        run: uv pip install --system -U https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip
      
      - name: Build firmware
        run: pio run
      
      - name: Find ESP32 device
        id: find-device
        run: |
          # PlatformIO auto-detects ESP32 devices
          port=$(pio device list | grep -i 'ESP32\|CP210x\|CH340' | head -1 | cut -d: -f1)
          echo "port=$port" >> $GITHUB_OUTPUT
      
      - name: Upload firmware
        run: pio run --target upload --upload-port ${{ steps.find-device.outputs.port }}
      
      - name: Run tests
        run: |
          pip install -r scripts/test_runner/requirements.txt
          python3 scripts/test_runner/test_runner.py \
            --port ${{ steps.find-device.outputs.port }} \
            --suite full
      
      - name: Upload results
        uses: actions/upload-artifact@v6
        with:
          name: test-results
          path: test-results/
          retention-days: 30
```

## File Changes Summary

| Action | File |
|--------|------|
| Create | `src/activities/tester/TestActivity.h` |
| Create | `src/activities/tester/TestActivity.cpp` |
| Modify | `src/main.cpp` (extend command handler) |
| Create | `scripts/test_runner/__init__.py` |
| Create | `scripts/test_runner/test_runner.py` |
| Create | `scripts/test_runner/serial_connector.py` |
| Create | `scripts/test_runner/test_suite.py` |
| Create | `scripts/test_runner/test_display.py` |
| Create | `scripts/test_runner/test_button.py` |
| Create | `scripts/test_runner/test_font.py` |
| Create | `scripts/test_runner/test_reader.py` |
| Create | `scripts/test_runner/compare_screenshots.py` |
| Create | `scripts/test_runner/find_device.py` |
| Create | `scripts/test_runner/requirements.txt` |
| Create | `.github/workflows/test.yml` |

## Verification Steps

1. **Build and flash**: `pio run --target upload`
2. **Enter test mode**: Send `CMD:TEST_MODE` via serial
3. **List tests**: Send `CMD:LIST_TESTS`
4. **Run test**: Send `CMD:RUN_TEST display,screen_refresh`
5. **Capture screenshot**: Send `CMD:SCREENSHOT test1`
6. **Verify output**: Screenshot should be sent back over serial

## Rollback Plan

- TestActivity uses existing ActivityManager APIs
- No changes to production activities
- Can be disabled by removing TestActivity references in main.cpp
- No persistent state changes on device

## Setting Up Self-Hosted Runners

### Option A: Docker Container (Recommended)

```bash
# Build the runner image
cd scripts/test_runner
docker build -f Dockerfile.runner -t github-runner .

# Run the container with USB device access
docker run -d \
  --name github-runner \
  --device /dev/ttyACM0 \
  --device /dev/ttyUSB0 \
  -v /path/to/repo:/home/runner/repo \
  -w /home/runner \
  github-runner \
  /bin/bash -c "./config.sh --url https://github.com/your-username/crosspoint-reader --token YOUR_TOKEN && ./run.sh"
```

### Option B: Direct Installation

```bash
# Download and configure the runner
mkdir actions-runner && cd actions-runner
curl -o actions-runner-linux-x64.tar.gz -L https://github.com/actions/runner/releases/latest/download/actions-runner-linux-x64.tar.gz
tar xzf actions-runner-linux-x64.tar.gz

# Configure (replace URL and token with your repo values)
./config.sh --url https://github.com/crosspoint-reader/crosspoint-reader --token YOUR_TOKEN

# Run the runner
./run.sh
```

The runner will automatically pick up jobs marked with `runs-on: self-hosted`.

## Security Considerations

| Concern | Mitigation |
|---------|------------|
| Token exposure in workflows | Don't use `pull_request` trigger; only trust your own branches |
| Code execution on host | Use Docker container for isolation |
| USB device access | Container has access to all USB devices passed via `--device` |
| Persistent runner access | Use strong machine security; consider VM/container boundaries |
