"""Execute the application loop on the host with mocked GPIO, sensor and clock."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import extract_ram_log

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
static uint32_t testTick;
static int16_t testTemperature = 800;
static uint8_t testSensorOk = 1;
static GPIO_PinState testSsr;
uint32_t HAL_GetTick(void) { return testTick; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state) {
  if (port == SSR_Heater_GPIO_Port && pin == SSR_Heater_Pin) testSsr = state;
}
uint8_t Max31865_ReadTemperatureTenthsC(Max31865Handle *handle, int16_t *value) {
  (void)handle; *value = testTemperature; return testSensorOk;
}
void tm1637DisplaySegments(TM1637Handle *handle, const uint8_t segments[4]) {
  (void)handle; (void)segments;
}
void tm1637DisplayDecimalTenths(TM1637Handle *handle, int value) {
  (void)handle; (void)value;
}
void ButtonInput_Update(ButtonInput *button, uint32_t now, uint32_t debounce,
                        uint32_t longPress, uint32_t repeat) {
  (void)button; (void)now; (void)debounce; (void)longPress; (void)repeat;
}
uint8_t ButtonInput_ConsumePressed(ButtonInput *button) {
  uint8_t value = button->pressedEvent; button->pressedEvent = 0; return value;
}
uint8_t ButtonInput_ConsumeRepeat(ButtonInput *button) {
  uint8_t value = button->repeating; button->repeating = 0; return value;
}
static void begin(uint32_t tick) {
  testTick = tick; testSensorOk = 1; testTemperature = 800;
  sensorReady = 1; safetyErrorActive = 0; temperatureFilterReady = 0;
  startupSafetyState = STARTUP_SAFETY_READY;
  heaterTestSelectedU = 51; HeaterTest_Start();
  assert(heaterTestRunning && gHeaterTestLog.sampleCount == 1);
  assert(gHeaterTestLog.samples[0].elapsedMs == 0);
}
static void process(uint32_t tick) { testTick = tick; HeaterTest_Process(tick); }
int main(int argc, char **argv) {
  assert(argc == 2);
  assert(sizeof(HeaterTestLog) == 86472);
  begin(0);
  assert(testSsr == GPIO_PIN_SET);
  HeaterTest_ApplyPower(1999); assert(testSsr == GPIO_PIN_SET);
  HeaterTest_ApplyPower(2000); assert(testSsr == GPIO_PIN_RESET);
  heaterTestSelectedU = 102;
  powerButtons[2].pressedEvent = 1;
  process(5000);
  assert(heaterTestControlU == 51 && heaterTestChangePending);
  /* Editing the candidate after P3 must not change the latched step. */
  heaterTestSelectedU = 103;
  process(9999); assert(heaterTestControlU == 51 && testSsr == GPIO_PIN_RESET);
  uint32_t eventIndex = gHeaterTestLog.sampleCount;
  process(10000);
  assert(heaterTestControlU == 102 && testSsr == GPIO_PIN_SET);
  assert(gHeaterTestLog.samples[eventIndex].status == HEATER_LOG_STATUS_POWER_CHANGE);
  assert(gHeaterTestLog.samples[eventIndex].elapsedMs == 10000);
  assert(gHeaterTestLog.samples[eventIndex].controlU == 102);
  HeaterTest_ApplyPower(13999); assert(testSsr == GPIO_PIN_SET);
  HeaterTest_ApplyPower(14000); assert(testSsr == GPIO_PIN_RESET);
  HeaterTest_Stop(HEATER_LOG_STATUS_STOP, 15000);
  assert(!heaterTestRunning && !heaterTestChangePending && gHeaterTestLog.complete);
  assert(testSsr == GPIO_PIN_RESET);
  assert(gHeaterTestLog.samples[gHeaterTestLog.sampleCount-1].heaterOn == 0);
  FILE *file = fopen(argv[1], "wb"); assert(file);
  assert(fwrite((const void *)&gHeaterTestLog, sizeof(gHeaterTestLog), 1, file) == 1);
  fclose(file);

  /* Scheduled readings retain raw temperature, independent of display filter. */
  begin(0);
  testTemperature = 900;
  for (uint32_t tick = 1; tick <= 3200; ++tick) process(tick);
  assert(gHeaterTestLog.sampleCount == 4);
  assert(gHeaterTestLog.samples[1].elapsedMs == 1200);
  assert(gHeaterTestLog.samples[2].elapsedMs == 2100);
  assert(gHeaterTestLog.samples[3].elapsedMs == 3000);
  assert(gHeaterTestLog.samples[1].temperatureTenthsC == 900);
  assert(filteredTemperatureTenthsC < rawTemperatureTenthsC);
  testTemperature = 1390;
  process(3300);
  assert(!heaterTestRunning && testSsr == GPIO_PIN_RESET);
  assert(filteredTemperatureTenthsC < 1380);
  assert(gHeaterTestLog.samples[gHeaterTestLog.sampleCount-1].status == HEATER_LOG_STATUS_OVER_TEMP);

  begin(0); testSensorOk = 0; process(300);
  assert(!heaterTestRunning && gHeaterTestLog.complete && testSsr == GPIO_PIN_RESET);
  assert(gHeaterTestLog.samples[1].status == HEATER_LOG_STATUS_SENSOR_ERROR);
  begin(0); gHeaterTestLog.sampleCount = HEATER_TEST_LOG_CAPACITY-2; process(1);
  assert(!heaterTestRunning && gHeaterTestLog.complete && testSsr == GPIO_PIN_RESET);
  assert(gHeaterTestLog.samples[HEATER_TEST_LOG_CAPACITY-2].status == HEATER_LOG_STATUS_BUFFER_FULL);
  begin(0); process(HEATER_TEST_DURATION_MS);
  assert(!heaterTestRunning && gHeaterTestLog.complete && testSsr == GPIO_PIN_RESET);
  assert(gHeaterTestLog.samples[1].status == HEATER_LOG_STATUS_DONE);

  /* Unsigned time arithmetic must survive HAL tick rollover. */
  begin(UINT32_MAX - 5000U);
  heaterTestSelectedU = 102; powerButtons[2].pressedEvent = 1;
  process(UINT32_MAX - 4000U); process(4999U);
  assert(heaterTestControlU == 102);
  assert(gHeaterTestLog.samples[2].elapsedMs == 10000);
  return 0;
}
'''


@unittest.skipUnless(shutil.which('gcc'), 'host GCC is required for the firmware harness')
class ManualFirmwareTest(unittest.TestCase):
    def test_loop_and_binary_format(self):
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            source = (ROOT / 'Core/Src/main.c').read_text().split('\nint main(void)')[0]
            # The remaining main/GPIO/clock initialization needs physical hardware;
            # execute the actual application functions above it with mocked HAL.
            (temp / 'harness.c').write_text(source + HARNESS)
            includes = ['Core/Inc', 'Drivers/STM32F4xx_HAL_Driver/Inc',
                        'Drivers/CMSIS/Include', 'Drivers/CMSIS/Device/ST/STM32F4xx/Include']
            command = ['gcc', '-DSTM32F407xx', '-DUSE_HAL_DRIVER',
                       '-Wno-int-to-pointer-cast', '-ffunction-sections', '-fdata-sections']
            for include in includes:
                command += ['-I', str(ROOT / include)]
            command += [str(temp / 'harness.c'), str(ROOT / 'Core/Src/heater_test_log.c'),
                        '-Wl,--gc-sections', '-o', str(temp / 'harness')]
            subprocess.run(command, check=True, capture_output=True, text=True)
            subprocess.run([str(temp / 'harness'), str(temp / 'log.bin')], check=True)
            rows, complete, _, capacity = extract_ram_log.decode((temp / 'log.bin').read_bytes())
            self.assertTrue(complete)
            self.assertEqual(capacity, 7204)
            event = next(row for row in rows if row[4] == 'POWER_CHANGE')
            self.assertEqual(event[0], 10000)
            self.assertEqual(event[-2:], (102, 255))
            self.assertEqual(rows[-1][2:5], (0, 80.0, 'STOP'))


if __name__ == '__main__':
    unittest.main()
