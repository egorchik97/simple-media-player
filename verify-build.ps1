Set-Location $PSScriptRoot
$command = '""' + $PSScriptRoot + '\build.cmd" > "' + $PSScriptRoot + '\build-verified.log" 2>&1"'
$p = Start-Process cmd.exe -ArgumentList ('/c ' + $command) -WorkingDirectory $PSScriptRoot -PassThru
$p.WaitForExit()
$global:LASTEXITCODE = $p.ExitCode
$code=$LASTEXITCODE
$code | Set-Content build.exit
exit $code
