$ErrorActionPreference = 'Stop'

$gcc = 'arm-none-eabi-gcc'
$objcopy = 'arm-none-eabi-objcopy'
$size = 'arm-none-eabi-size'
$build = '..\build\bootloader'

New-Item -ItemType Directory -Force -Path $build | Out-Null

$cflags = @(
  '-mcpu=cortex-m3',
  '-mthumb',
  '-DUSE_HAL_DRIVER',
  '-DSTM32F103xB',
  '-ICore/Inc',
  '-IFramework/Inc',
  '-I../Drivers/STM32F1xx_HAL_Driver/Inc',
  '-I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy',
  '-I../Drivers/CMSIS/Device/ST/STM32F1xx/Include',
  '-I../Drivers/CMSIS/Include',
  '-Og',
  '-Wall',
  '-fdata-sections',
  '-ffunction-sections',
  '-g',
  '-gdwarf-2'
)

$sources = @(
  'Core/Src/main.c',
  'Framework/Src/boot_jump.c',
  'Framework/Src/boot_framework_led.c',
  'Framework/Src/boot_framework_clock.c',
  'Framework/Src/boot_framework_power.c',
  'Core/Src/stm32f1xx_it.c',
  'Core/Src/stm32f1xx_hal_msp.c',
  'Core/Src/system_stm32f1xx.c',
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c',
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c',
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c',
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c',
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c',
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c',
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c',
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c',
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_exti.c'
)

$objects = @()
foreach ($src in $sources) {
  $base = [IO.Path]::GetFileNameWithoutExtension($src)
  $obj = Join-Path $build "$base.o"
  & $gcc -c @cflags $src -o $obj
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  $objects += $obj
}

$asmObj = Join-Path $build 'startup_stm32f103xb.o'
& $gcc -x assembler-with-cpp -c @cflags 'startup_stm32f103xb.s' -o $asmObj
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$objects += $asmObj

$elf = Join-Path $build 'bootloader.elf'
$hex = Join-Path $build 'bootloader.hex'
$bin = Join-Path $build 'bootloader.bin'

& $gcc @objects -mcpu=cortex-m3 -mthumb '-TSTM32F103CBTx_BOOT_FLASH.ld' -lc -lm -lnosys "-Wl,-Map=$build/bootloader.map,--cref" '-Wl,--gc-sections' -o $elf
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $size $elf
& $objcopy -O ihex $elf $hex
& $objcopy -O binary -S $elf $bin

Get-ChildItem "$build\bootloader.*" | Select-Object Name,Length
