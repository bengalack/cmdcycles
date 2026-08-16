@echo Make sure MSXDOS is present in the dska-folder
@echo Machines with only 64kB needs more ram for MSX DOS2 to work (fixed by -ext)

@REM set MACHINE=Sanyo_phc-70FD2
@REM set MACHINE=Philips_NMS_8255
set MACHINE=Panasonic_FS-A1GT
@REM set MACHINE=Sony_HB-F1XD
@REM set MACHINE=Panasonic_FS-A1FM
@REM set MACHINE=Al_Alamiah_AX370

@REM make a run script which incorporates the name of the model
@rem but ensure the filename is short (use model only - text after last underscore)
@rem cc.bat is run from autoexec.bat
@set SHORTNAME=%MACHINE%
:loop
@for /f "tokens=1* delims=_" %%A in ("%SHORTNAME%") do @if not "%%B"=="" (set SHORTNAME=%%B& goto loop)

@rem Remove any remaining spaces from SHORTNAME
@set SHORTNAME=%SHORTNAME: =%

@echo cmdcycle %MACHINE% ^> %SHORTNAME%.md > dska\cc.bat
@REM @echo cmdcycle %MACHINE% > dska\cc.bat

C:\tools\openmsx21x\openmsx.exe -machine %MACHINE% -ext msxdos2 -ext ram1mb -diska dska/ -command "debug set_watchpoint read_io 0x2E" -command "set throttle off" -command "after time 10 {set throttle on}"
@REM C:\tools\openmsx21x\openmsx.exe -machine %MACHINE% -ext msxdos2 -ext ram1mb -diska dska/ -command "debug set_watchpoint read_io 0x2E" -command "set throttle off"