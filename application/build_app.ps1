param(
  [ValidateSet('slave', 'master')]
  [string]$Role = 'slave',
  [int]$LocalId = 0,
  [int]$RemoteId = 0
)

$ErrorActionPreference = 'Stop'

$gcc = 'arm-none-eabi-gcc'
$objcopy = 'arm-none-eabi-objcopy'
$size = 'arm-none-eabi-size'
$build = "..\build\application-$Role"
$output = '..\build\application'

New-Item -ItemType Directory -Force -Path $build | Out-Null
New-Item -ItemType Directory -Force -Path $output | Out-Null

if ($Role -eq 'master') {
  if ($LocalId -eq 0) { $LocalId = 1 }
  if ($RemoteId -eq 0) { $RemoteId = 2 }
  $roleDefines = @(
    '-DAPP_RADIO_DEVICE_ROLE=APP_RADIO_ROLE_MASTER',
    "-DAPP_RADIO_LOCAL_ID=${LocalId}U",
    "-DAPP_RADIO_REMOTE_ID=${RemoteId}U"
  )
} else {
  if ($LocalId -eq 0) { $LocalId = 2 }
  if ($RemoteId -eq 0) { $RemoteId = 1 }
  $roleDefines = @(
    '-DAPP_RADIO_DEVICE_ROLE=APP_RADIO_ROLE_SLAVE',
    "-DAPP_RADIO_LOCAL_ID=${LocalId}U",
    "-DAPP_RADIO_REMOTE_ID=${RemoteId}U"
  )
}

$cflags = @(
  '-mcpu=cortex-m3',
  '-mthumb',
  '-DUSE_HAL_DRIVER',
  '-DSTM32F103xB',
  '-DUSER_VECT_TAB_ADDRESS',
  '-DVECT_TAB_OFFSET=0x00004000U',
  $roleDefines,
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

Copy-Item -LiteralPath $elf -Destination (Join-Path $output "application-$Role.elf") -Force
Copy-Item -LiteralPath $hex -Destination (Join-Path $output "application-$Role.hex") -Force
Copy-Item -LiteralPath $bin -Destination (Join-Path $output "application-$Role.bin") -Force
Copy-Item -LiteralPath (Join-Path $build 'application.map') -Destination (Join-Path $output "application-$Role.map") -Force
Copy-Item -LiteralPath $hex -Destination (Join-Path $output 'application.hex') -Force
