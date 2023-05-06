# escape=`

# Modified from https://code.qt.io/cgit/qbs/qbs.git/plain/docker/windowsservercore/Dockerfile
# and https://stackoverflow.com/questions/66852183/use-docker-windows-for-gitlab-runner
FROM mcr.microsoft.com/windows/servercore:1809
LABEL Description="Windows Server Core development environment"

USER ContainerAdministrator

# Disable crash dialog for release-mode runtimes
RUN reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting" /v Disabled /t REG_DWORD /d 1 /f
RUN reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting" /v DontShowUI /t REG_DWORD /d 1 /f

# Download the Build Tools bootstrapper.
ADD https://aka.ms/vs/16/release/vs_buildtools.exe C:\TEMP\vs_buildtools.exe
# Install Build Tools with the Microsoft.VisualStudio.Workload.AzureBuildTools workload, excluding workloads and components with known issues.
RUN C:\TEMP\vs_buildtools.exe --quiet --wait --norestart --nocache `
    --installPath C:\BuildTools `
    --add Microsoft.VisualStudio.Workload.VCTools `
    --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    --add Microsoft.VisualStudio.Component.VC.CMake.Project `
    --add Microsoft.VisualStudio.Component.Windows10SDK.19041 `
    --locale en-US `
 || IF "%ERRORLEVEL%"=="3010" EXIT 0

# Install Mambaforge
ADD "https://github.com/conda-forge/miniforge/releases/latest/download/Mambaforge-Windows-x86_64.exe" C:\\TEMP\\mambaforge.exe
RUN C:\TEMP\mambaforge.exe `
    /InstallationType=AllUsers `
    /AddToPath=1 `
    /RegisterPython=1 `
    /S `
    /D=C:\Miniconda

# Install additional conda packages
RUN setx /M PATH "%PATH%;C:\Miniconda;C:\Miniconda\Scripts;C:\Miniconda\Library\bin"
RUN mamba install -y conda-build boa anaconda-client ninja cmake doxygen

# Install Chocolatey.
RUN powershell -NoProfile -ExecutionPolicy Bypass -Command `
    $Env:chocolateyVersion = '0.10.15' ; `
    $Env:chocolateyUseWindowsCompression = 'false' ; `
    "[Net.ServicePointManager]::SecurityProtocol = \"tls12, tls11, tls\"; iex ((New-Object System.Net.WebClient).DownloadString('http://chocolatey.org/install.ps1'))" && SET "PATH=%PATH%;%ALLUSERSPROFILE%\chocolatey\bin"

# Install various packages.
RUN choco install -y 7zip &&`
    choco install -y pwsh &&`
    choco install -y git

RUN conda init powershell
CMD ["pwsh.exe"]
ENTRYPOINT ["C:\\Windows\\system32\\WindowsPowerShell\\v1.0\\powershell.exe"]
