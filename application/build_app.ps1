$ErrorActionPreference = 'Stop'

$gcc = 'arm-none-eabi-gcc'
$objcopy = 'arm-none-eabi-objcopy'
$size = 'arm-none-eabi-size'
$build = '..\build\application'
$output = '..\build\application'

New-Item -ItemType Directory -Force -Path $build | Out-Null
New-Item -ItemType Directory -Force -Path $output | Out-Null

$cflags = @(
  '-mcpu=cortex-m3',
  '-mthumb',
  '-DUSE_HAL_DRIVER',
  '-DSTM32F103xB',
  '-DUSER_VECT_TAB_ADDRESS',
  '-DVECT_TAB_OFFSET=0x00004000U',
  '-ICore/Inc',
  '-IApp/Inc',
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
  'App/Src/app.c',
  'Framework/Src/app_framework_led.c',
  'Framework/Src/app_framework_clock.c',
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
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_exti.c',
  '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c'
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

$elf = Join-Path $build 'application.elf'
$hex = Join-Path $build 'application.hex'
$bin = Join-Path $build 'application.bin'

& $gcc @objects -mcpu=cortex-m3 -mthumb '-TSTM32F103CBTx_APP_FLASH.ld' -lc -lm -lnosys "-Wl,-Map=$build/application.map,--cref" '-Wl,--gc-sections' -o $elf
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $size $elf
& $objcopy -O ihex $elf $hex
& $objcopy -O binary -S $elf $bin

Get-ChildItem "$build\application.*" | Select-Object Name,Length
