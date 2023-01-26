Overview
========
Linux, macOS, amd windows versions of the libarby are all build with docker, in a shell, and with docker-windows (on Server2019 with process level isolation), respectivel. Linux versions (x86, arm64, ppc64le) are build with the mambaforge docker (condaforge/mambaforge). MacOS versions of the libary are build in a VM in a conda-forge environemnt. Windows versions are build in a custom build docker container (Docker file included here).

Installation
============

Linux GitLabRunner
------------------
Install a Linux distribution (here Ubuntu 18.04) and Docker. To install docker follow

https://docs.docker.com/engine/install/ubuntu/

Next, install the GitLabRunner. For that, login as root/admin into your Gitlab installation. Navigate to the administration page->Runner->Register runner. 


Windows GitLabRunner
-------------------
A GitlabRunner on Windows Server 2019 builds the library using a docker-windows executer running on docker in process isolation (not Hyper-V).

Docker on Windows Server 2019 is described as follows (see:
https://computingforgeeks.com/how-to-run-docker-containers-on-windows-server-2019/)

In a Powershell with admin rights execute:

Install-Module -Name DockerMsftProvider -Repository PSGallery -Force

and

```
Install-Package -Name docker -ProviderName DockerMsftProvider
```

then restart the computer

```
Restart-Computer -Force
```

The gitlab runner is installed as described on the gitlab host (admin->gitlab runner-> register runner)

The docker container (used to build the library) is build by executing in the folder with the Dockerfile

```
docker build -t mambaforge:vs16 .
```

Optionally, you can create a "c:\builds" directory and modifiy the config.toml of the gitlab runner as follows. Here, we map the local "c:\builds" directory to the docker container to make sure that we can only update a already pulled git epository. Elsewise, c:\builds in the docker will be always newly pulled. Adjust the number of concurrent to your machine.

```
concurrent = 2

[session_server]
  session_timeout = 3600

[[runners]]
  builds_dir = "C:\\builds\\"
  [runners.docker]
    pull_policy = ["if-not-present"]
    tls_verify = false
    image = "mambaforge:vs16"
    privileged = false
    disable_entrypoint_overwrite = false
    oom_kill_disable = false
    disable_cache = false
    volumes = ["c:\\cache", "C:\\builds\\:C:\\builds\\"]
    shm_size = 0
```


