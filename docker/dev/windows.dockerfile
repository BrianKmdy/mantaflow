# escape=`

# Use the latest Windows Server Core 2019 image.
FROM mcr.microsoft.com/windows/servercore:ltsc2022

# Restore the default Windows shell for correct batch processing.
SHELL ["cmd", "/S", "/C"]
RUN `
    # Download the Build Tools bootstrapper.
    curl -SL --output vs_buildtools.exe https://aka.ms/vs/17/release/vs_buildtools.exe `
    `
    # Install Build Tools with the Microsoft.VisualStudio.Workload.AzureBuildTools workload, excluding workloads and components with known issues.
    && (start /w vs_buildtools.exe --quiet --wait --norestart --nocache `
        --installPath "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools" `
        --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended `
        --remove Microsoft.VisualStudio.Component.Windows10SDK.10240 `
        --remove Microsoft.VisualStudio.Component.Windows10SDK.10586 `
        --remove Microsoft.VisualStudio.Component.Windows10SDK.14393 `
        --remove Microsoft.VisualStudio.Component.Windows81SDK `
        || IF "%ERRORLEVEL%"=="3010" EXIT 0) `
    `
    # Cleanup
    && del /q vs_buildtools.exe

# Install chocolatey
RUN @"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -InputFormat None -ExecutionPolicy Bypass -Command "[System.Net.ServicePointManager]::SecurityProtocol = 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))" && SET "PATH=%PATH%;%ALLUSERSPROFILE%\chocolatey\bin"

# Install dependencies with chocolatey
RUN choco install cmake --version 3.24.2 -y --installargs 'ADD_CMAKE_TO_PATH=System'
RUN choco install python --version 3.10.1 -y
RUN choco install doxygen.install --version 1.9.5 -y
RUN choco install 7zip --version 22.1 -y
RUN choco install git --version 2.37.3 -y
RUN setx path "C:\\Program Files\\Git\\bin;%path%"

# Download and extract qt5
WORKDIR c:\Qt
RUN @"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -InputFormat None -ExecutionPolicy Bypass -Command `
        Invoke-WebRequest -Method Get -Uri https://github.com/martinrotter/qt-minimalistic-builds/releases/download/5.15.3/qt-5.15.3-static-msvc2019-x86_64.7z -OutFile qt5.7z
RUN 7z x qt5.7z

# Set the starting directory and run bash
WORKDIR c:\project
ENTRYPOINT ["bash.exe"]