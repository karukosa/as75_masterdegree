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
  assert(argc == 3);
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
  for (uint32_t tick = 1; tick <= 30200; ++tick) process(tick);
  assert(gHeaterTestLog.sampleCount == 4);
  assert(gHeaterTestLog.samples[1].elapsedMs == 10200);
  assert(gHeaterTestLog.samples[2].elapsedMs == 20100);
  assert(gHeaterTestLog.samples[3].elapsedMs == 30000);
  assert(gHeaterTestLog.samples[1].temperatureTenthsC == 900);
  assert(filteredTemperatureTenthsC < rawTemperatureTenthsC);
  testTemperature = 1390;
  process(30300);
  assert(!heaterTestRunning && testSsr == GPIO_PIN_RESET);
  assert(filteredTemperatureTenthsC < 1380);
  assert(gHeaterTestLog.samples[gHeaterTestLog.sampleCount-1].status == HEATER_LOG_STATUS_OVER_TEMP);

  begin(0); testSensorOk = 0; process(300);
  assert(!heaterTestRunning && gHeaterTestLog.complete && testSsr == GPIO_PIN_RESET);
  assert(gHeaterTestLog.samples[1].status == HEATER_LOG_STATUS_SENSOR_ERROR);
  begin(0); gHeaterTestLog.sampleCount = HEATER_TEST_LOG_CAPACITY-2; process(1);
  assert(!heaterTestRunning && gHeaterTestLog.complete && testSsr == GPIO_PIN_RESET);
  assert(gHeaterTestLog.samples[HEATER_TEST_LOG_CAPACITY-2].status == HEATER_LOG_STATUS_BUFFER_FULL);

  /* A cold-start run must retain the entire transient, including its first point.
   * Exercise every 300 ms safety check through 20 hours, beyond the old 2 h stop. */
  begin(0);
  testTemperature = 250;
  heaterTestSelectedU = 77;
  HeaterTest_Start();
  assert(gHeaterTestLog.samples[0].temperatureTenthsC == 250);
  const uint32_t twentyHours = 20U * 60U * 60U * 1000U;
  for (uint32_t tick = 300; tick <= twentyHours; tick += 300) {
    process(tick);
    assert(heaterTestRunning && !gHeaterTestLog.complete);
    if (tick == 2U * 60U * 60U * 1000U) {
      assert(gHeaterTestLog.sampleCount == 721);
    }
  }
  assert(gHeaterTestLog.sampleCount == 7201);
  assert(gHeaterTestLog.samples[7200].elapsedMs == twentyHours);
  /* Never wrap or overwrite the beginning when the finite RAM log fills. */
  for (uint32_t tick = twentyHours+300; heaterTestRunning; tick += 300) {
    assert(tick < twentyHours+20000);
    process(tick);
  }
  assert(gHeaterTestLog.complete && testSsr == GPIO_PIN_RESET);
  assert(gHeaterTestLog.sampleCount == HEATER_TEST_LOG_CAPACITY-1);
  assert(gHeaterTestLog.samples[0].elapsedMs == 0);
  assert(gHeaterTestLog.samples[0].temperatureTenthsC == 250);
  assert(gHeaterTestLog.samples[gHeaterTestLog.sampleCount-1].status == HEATER_LOG_STATUS_BUFFER_FULL);
  file = fopen(argv[2], "wb"); assert(file);
  assert(fwrite((const void *)&gHeaterTestLog, sizeof(gHeaterTestLog), 1, file) == 1);
  fclose(file);

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
            subprocess.run([str(temp / 'harness'), str(temp / 'log.bin'),
                            str(temp / 'long_log.bin')], check=True)
            rows, complete, _, capacity = extract_ram_log.decode((temp / 'log.bin').read_bytes())
            self.assertTrue(complete)
            self.assertEqual(capacity, 7204)
            event = next(row for row in rows if row[4] == 'POWER_CHANGE')
            self.assertEqual(event[0], 10000)
            self.assertEqual(event[-2:], (102, 255))
            self.assertEqual(rows[-1][2:5], (0, 80.0, 'STOP'))
            long_rows, complete, _, capacity = extract_ram_log.decode((temp / 'long_log.bin').read_bytes())
            self.assertTrue(complete)
            self.assertEqual(capacity, 7204)
            self.assertEqual(len(long_rows), 7203)
            self.assertEqual(long_rows[0], (0, 30, 1, 25.0, 'RUN', 77, 255))
            self.assertEqual(long_rows[7200][0], 72_000_000)
            self.assertEqual(long_rows[-1][4], 'BUFFER_FULL')
            self.assertEqual(long_rows[-1][2], 0)
            intervals = {b[0]-a[0] for a, b in zip(long_rows[:-2], long_rows[1:-1])}
            self.assertEqual(intervals, {9900, 10200})


if __name__ == '__main__':
    unittest.main()
