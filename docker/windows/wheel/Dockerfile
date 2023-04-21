# escape=`

# Modified from https://code.qt.io/cgit/qbs/qbs.git/plain/docker/windowsservercore/Dockerfile
# and https://stackoverflow.com/questions/66852183/use-docker-windows-for-gitlab-runner
# ltsc2022
# use 1809 for windows server 2019
# user ltsc2022 for windows 11
FROM mcr.microsoft.com/windows/servercore:1809
LABEL Description="Windows Server Core development environment"

USER ContainerAdministrator

# Disable crash dialog for release-mode runtimes
RUN reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting" /v Disabled /t REG_DWORD /d 1 /f
RUN reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting" /v DontShowUI /t REG_DWORD /d 1 /f

# Download the Build Tools bootstrapper. VS 2019
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

# Install Chocolatey.
RUN powershell -NoProfile -ExecutionPolicy Bypass -Command `
    $Env:chocolateyVersion = '0.10.15' ; `
    $Env:chocolateyUseWindowsCompression = 'false' ; `
    "[Net.ServicePointManager]::SecurityProtocol = \"tls12, tls11, tls\"; iex ((New-Object System.Net.WebClient).DownloadString('http://chocolatey.org/install.ps1'))" && SET "PATH=%PATH%;%ALLUSERSPROFILE%\chocolatey\bin"

# Install various packages.
RUN choco install -y pwsh
RUN choco install -y 7zip
RUN choco install -y git
RUN choco install -y python --version=3.8.6
RUN choco install -y nano

RUN pip install cibuildwheel twine

RUN powershell iwr -o c:\boost.exe https://netcologne.dl.sourceforge.net/project/boost/boost-binaries/1.74.0/boost_1_74_0-msvc-14.2-64.exe
RUN .\boost.exe /sp- /verysilent /suppressmsgboxes /norestart | more
RUN del c:\boost.exe

# RUN md fftw && powershell iwr -o c:\fftw\fftw.zip https://fftw.org/pub/fftw/fftw-3.3.5-dll64.zip
# RUN cd fftw && 7z x fftw.zip & del fftw.zip && cd ..

RUN choco install -y swig

# RUN powershell iwr -o c:\hdf5.zip https://support.hdfgroup.org/ftp/HDF5/releases/hdf5-1.12/hdf5-1.12.2/bin/windows/hdf5-1.12.2-Std-win10_64-vs16-Intel.zip
# RUN 7z x hdf5.zip && del hdf5.zip
# msiexec /i C:\hdf\HDF5-1.12.2-win64.msi /quiet | more

RUN mkdir c:\wheelhouse
CMD ["cmd.exe"]
ENTRYPOINT ["C:\\Windows\\system32\\WindowsPowerShell\\v1.0\\powershell.exe"]