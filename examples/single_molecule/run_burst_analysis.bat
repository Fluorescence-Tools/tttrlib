@echo off
setlocal

:: Set the TTTRLIB_DATA environment variable
set TTTRLIB_DATA=Q:\tttr-data

:: Print the environment variable for verification
echo TTTRLIB_DATA is set to: %TTTRLIB_DATA%

:: Check if the directory exists
if not exist "%TTTRLIB_DATA%" (
    echo Error: Directory %TTTRLIB_DATA% does not exist.
    echo Please make sure the path is correct and accessible.
    pause
    exit /b 1
)

:: Run the Python script
echo Running burst analysis...
python plot_01_burst_analysis.py

:: Keep the window open to see the output
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Script failed with error code %ERRORLEVEL%
    pause
) else (
    echo.
    echo Script completed successfully
    pause
)
