---
title: 如何制作Windows11镜像并在KubeVirt中部署
date: 2025-07-07 21:52:01
tags: [windows11,kubevirt,边做边学]
---

> 最近需要在k8s中解锁win11虚拟机，所以进行了相关学习并记录下此篇博客

# win11虚拟机配置

本地先安装win11虚拟机并准备所需要的软件，这里使用linux来创建win11虚拟机

```bash
virt-install \
 --virt-type=kvm \
 --name win11-render-1.6 \
 --ram 16384 \
 --vcpus=8 \
 --os-variant=win11  \
 --cdrom=/data/render/Win11_24H2_Chinese_Simplified_x64.iso \
 --network=default,model=virtio \
 --graphics vnc,listen=0.0.0.0 --noautoconsole \
 --disk path=/data/render/virtio-win-0.1.271.iso,device=cdrom \
 --disk path=/data/render/win11-render-1.6.qcow2,size=256,bus=virtio,format=qcow2 \
 --boot uefi # win11默认使用uefi启动
```

这里有一个要注意的点是启动的时候除了挂载win11的iso还要挂载virtio-win的驱动，不然会出现个别硬件无法使用的问题

virtio-win下载地址：[https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/archive-virtio/](https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/archive-virtio/)

## 驱动安装

参考文档

[https://kubevirt.io/user-guide/user_workloads/windows_virtio_drivers/](https://kubevirt.io/user-guide/user_workloads/windows_virtio_drivers/)

在安装系统的时候会出现无可选磁盘的问题，这个时候就要安装virtio-win里面的磁盘驱动（vioscsi），同时也可以把网络驱动给安装了(NetKvm)

![](./install-vioscsi.png)

进到系统后打开设备管理器看看那些设备需要安装驱动（从virtio-win里面安装）

![](./driver.png)

## 解锁BitLocker

成功进入系统后先别急着对系统进行定制安装，先把磁盘的BitLocker给关闭掉，以便后面进行sysprep打包

在powershell中输入

```powershell
# 查看磁盘解锁状态
manage-bde -status
BitLocker 驱动器加密: 配置工具版本 10.0.26100
版权所有 (C) 2013 Microsoft Corporation。保留所有权利。

可以使用 BitLocker 驱动器加密
保护的磁盘卷:
卷 C: []
[OS 卷]

    大小:              255.19 GB
    BitLocker 版本:    无
    转换状态:          完全解密
    已加密百分比:      0.0%  # 查看解密进度
    加密方法:          无
    保护状态:          保护关闭
    锁定状态:          已解锁
    标识字段:          无
    密钥保护器:        找不到
    
# 关闭对应磁盘BitLocker
manage-bde -off C:
```

## cloudbase-init 安装

在完成系统定制化软件安装后，下载cloudbase-init进行系统配置 [https://cloudbase.it/cloudbase-init/](https://cloudbase.it/cloudbase-init/)

使用cloudbase-init可以读取虚拟机启动时的配置进行动态配置系统信息（比如hostname、用户密码等）

![](./cloudbase1.png)

如果你这个时候已经配置好系统了，可以勾选下面的选项，它将会自动执行sysprep并关机

![](./cloudbase2.png)

### 配置cloudbase-init

 cloudbase-init 的典型用法主要是通过配置文件进行配置，以确定要使用哪些服务（services）和插件（plugins），然后在这些文件中设置参数来定制它们的行为以及整体初始化过程  

[https://cloudbase-init.readthedocs.io/en/latest/](https://cloudbase-init.readthedocs.io/en/latest/)

这里进行默认用户密码修改和hostname的配置

首先先配置对应的配置文件 cloudbase-init.conf ，这里只用配置 cloudbase-init.conf ，不用配置cloudbase-init-unattend.conf 

因为我们这使用的plugins是 **cloudbaseinit.plugins.common.sethostname.SetHostNamePlugin** 、**cloudbaseinit.plugins.common.setuserpassword.SetUserPasswordPlugin** 和 **cloudbaseinit.plugins.common.userdata.UserDataPlugin**

而cloudbase-init-unattend.conf中只能使用**MTU** 和 **hostname** plugins

因为要在本地验证配置，所以使用的service是  **cloudbaseinit.metadata.services.nocloudservice.NoCloudConfigDriveService**

[https://cloudbase-init.readthedocs.io/en/latest/tutorial.html#configuration-file](https://cloudbase-init.readthedocs.io/en/latest/tutorial.html#configuration-file)

```plain
# cloudbase-init.conf  metadata_services和plugins等相关配置
metadata_services=cloudbaseinit.metadata.services.nocloudservice.NoCloudConfigDriveService
plugins=cloudbaseinit.plugins.common.sethostname.SetHostNamePlugin,cloudbaseinit.plugins.common.setuserpassword.SetUserPasswordPlugin,cloudbaseinit.plugins.windows.updates.WindowsAutoUpdatesPlugin,cloudbaseinit.plugins.common.userdata.UserDataPlugin
allow_reboot=false    # allow the service to reboot the system
stop_service_on_exit=false
```

### 准备 <font style="color:rgb(0, 0, 0);">NoCloudConfigDriveService 数据</font>

> <font style="color:rgb(64, 64, 64);background-color:rgb(252, 252, 252);">The metadata is provided on a config-drive (vfat or iso9660) with the label cidata or CIDATA.</font>
>
> <font style="color:rgb(64, 64, 64);background-color:rgb(252, 252, 252);">The default folder structure for NoCloud is:</font>
>
> + <font style="color:rgb(64, 64, 64);background-color:rgb(252, 252, 252);">/user-data</font>
> + <font style="color:rgb(64, 64, 64);background-color:rgb(252, 252, 252);">/meta-data</font>

这个 `NoCloudConfigDriveServic ` 数据是一个iso磁盘，并且名称要为 cidata 或 CIDATA

<font style="color:rgb(0, 0, 0);">里面的文件可以有：meta-data(元数据) 和user-data(用户定义的数据)</font>

[https://cloudinit.readthedocs.io/en/latest/reference/datasources/nocloud.html](https://cloudinit.readthedocs.io/en/latest/reference/datasources/nocloud.html)

[https://cloudbase-init.readthedocs.io/en/latest/plugins.html#user-data-main](https://cloudbase-init.readthedocs.io/en/latest/plugins.html#user-data-main)

以下就是这次meta_data和user_data的配置

```shell
# cat meta_data.yaml 
instance-id: test-windows
hostname: test-windows
# cat user_data.yaml
#cloud-config
set_hostname: test-windows

users:
  - name: admin
    passwd: 1qaz@WSX # 对应用户的初始化密码
    groups:
      - Administrators

runcmd:
  - 'powershell -command "Write-Host \"Cloudbase-Init configuration applied successfully!\""'
```

进行iso打包

[https://documentation.ubuntu.com/public-images/public-images-how-to/use-local-cloud-init-ds/](https://documentation.ubuntu.com/public-images/public-images-how-to/use-local-cloud-init-ds/)

```shell
# 下载 cloud-localds
# dnf --enablerepo=devel install cloud-utils
# cloud-localds config-drive.iso user_data.yaml meta_data.yaml
```

把打包出来的config-drive.iso挂载到对应的虚拟机里面

```shell
# virsh edit --domain win11-test-cloudbase-init 
...
    <disk type='file' device='cdrom'>
      <driver name='qemu' type='raw'/>
      <source file='/data/render/cloudbase-init/config-drive.iso'/>
      <target dev='sdd' bus='sata'/>
      <readonly/>
      <address type='drive' controller='0' bus='0' target='0' unit='3'/>
    </disk>
...
```

![](./disk.png)

```powershell
PS G:\> dir

    目录: G:\

Mode                 LastWriteTime         Length Name
----                 -------------         ------ ----
--r---          2025/7/6     23:32             49 meta-data
--r---          2025/7/6     23:32            224 user-data
```

接下来可以按照项目要求安装软件，比如在windows上面安装openssh方便使用ansible进行管理

[使用ansible管理windwos - kehaha-5](https://kehaha-5.github.io/2025/07/06/使用ansible管理windwos/)

## sysprep系统封装

手动进行sysprep系统封装

[https://learn.microsoft.com/zh-cn/windows-hardware/manufacture/desktop/sysprep--system-preparation--overview?view=windows-11](https://learn.microsoft.com/zh-cn/windows-hardware/manufacture/desktop/sysprep--system-preparation--overview?view=windows-11)

命令文档：[https://learn.microsoft.com/zh-cn/windows-hardware/manufacture/desktop/sysprep-command-line-options?view=windows-11](https://learn.microsoft.com/zh-cn/windows-hardware/manufacture/desktop/sysprep-command-line-options?view=windows-11)

如果你的系统想要第一次进入时进行重新配置(oobe)就执行 

oobe: <font style="color:rgb(22, 22, 22);">客户可以通过开箱即用体验 (OOBE) 添加用户特定的信息并接受 Microsoft 软件许可条款</font>

```powershell
%WINDIR%\system32\sysprep\sysprep.exe /generalize /shutdown /oobe /mode:vm
```

如果想要系统第一次进入时进入审核模式，方便系统调式就执行

```powershell
%WINDIR%\system32\sysprep\sysprep.exe /generalize /shutdown /oobe /mode:vm
```

如果想要进入到无人接触模式就要配置好unattend文件，同时命令中指定对应的文件

```powershell
%WINDIR%\system32\sysprep\sysprep.exe /generalize /oobe /shutdown /unattend:xxx /mode:vm
```

这里使用的是无人接触模式，如果没有其它特殊的配置，unattend.xml可以用cloudbase-init安装时会携带的unattend.xml文件

注意必须要添加 `/mode:vm` 参数，否则可能会出现启动蓝屏（code:INACCESSIBLE BOOT DEVICE）或者出现提示系统无法启动，请重新安装windwos等无法启动的问题

这里使用添加 `/shutdown` 参数确保系统打包好后自动关机

## 配置验证

在配置完上述的CloudBase-Init和Sysprep打包关机后，手动启动系统，在系统启动完成后可以在cloudbase-init的log目录下面查看对应的执行日志

```log
2025-07-07 14:59:04.852 3768 INFO cloudbaseinit.metadata.services.osconfigdrive.windows [-] Config Drive found on G:\
2025-07-07 14:59:04.857 3768 DEBUG cloudbaseinit.metadata.services.baseconfigdrive [-] Metadata copied to folder: 'C:\\WINDOWS\\TEMP\\tmpbxgu6_cf' load C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\metadata\services\baseconfigdrive.py:81
2025-07-07 14:59:04.862 3768 INFO cloudbaseinit.init [-] Metadata service loaded: 'NoCloudConfigDriveService'
2025-07-07 14:59:04.864 3768 DEBUG cloudbaseinit.init [-] Instance id: test-windows configure_host C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\init.py:202
2025-07-07 14:59:04.869 3768 DEBUG cloudbaseinit.utils.classloader [-] Loading class 'cloudbaseinit.plugins.common.sethostname.SetHostNamePlugin' load_class C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\utils\classloader.py:35
2025-07-07 14:59:04.873 3768 DEBUG cloudbaseinit.utils.classloader [-] Loading class 'cloudbaseinit.plugins.common.setuserpassword.SetUserPasswordPlugin' load_class C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\utils\classloader.py:35
2025-07-07 14:59:04.876 3768 DEBUG cloudbaseinit.utils.classloader [-] Loading class 'cloudbaseinit.plugins.windows.updates.WindowsAutoUpdatesPlugin' load_class C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\utils\classloader.py:35
2025-07-07 14:59:04.882 3768 DEBUG cloudbaseinit.utils.classloader [-] Loading class 'cloudbaseinit.plugins.common.userdata.UserDataPlugin' load_class C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\utils\classloader.py:35
2025-07-07 14:59:04.887 3768 INFO cloudbaseinit.init [-] Executing plugins for stage 'MAIN':
2025-07-07 14:59:04.888 3768 DEBUG cloudbaseinit.init [-] Plugin 'SetHostNamePlugin' execution already done, skipping _exec_plugin C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\init.py:61
2025-07-07 14:59:04.894 3768 DEBUG cloudbaseinit.init [-] Plugin 'SetUserPasswordPlugin' execution already done, skipping _exec_plugin C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\init.py:61
2025-07-07 14:59:04.899 3768 DEBUG cloudbaseinit.init [-] Plugin 'WindowsAutoUpdatesPlugin' execution already done, skipping _exec_plugin C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\init.py:61
2025-07-07 14:59:04.902 3768 DEBUG cloudbaseinit.init [-] Plugin 'UserDataPlugin' execution already done, skipping _exec_plugin C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\init.py:61
2025-07-07 14:59:04.906 3768 DEBUG cloudbaseinit.metadata.services.baseconfigdrive [-] Deleting metadata folder: 'C:\\WINDOWS\\TEMP\\tmpbxgu6_cf' cleanup C:\Program Files\Cloudbase Solutions\Cloudbase-Init\Python\Lib\site-packages\cloudbaseinit\metadata\services\baseconfigdrive.py:93
2025-07-07 14:59:04.911 3768 INFO cloudbaseinit.init [-] Plugins execution done
```

从上面的日志可以看到，找到了Config Drive （G盘），获取了hostname为`test-windows`，并成功启动了 `UserDataPlugin`、`SetHostNamePlugin` 、`SetUserPasswordPlugin`  这几个插件

> 注意如果想要hostname配置成功，可能需要重新系统

[https://cloudbase-init.readthedocs.io/en/latest/userdata.html#cloud-config](https://cloudbase-init.readthedocs.io/en/latest/userdata.html#cloud-config)

# kubevirt配置

此时就完成了系统的封装，接下来把对应的qcow2镜像导出到webserver，方便后续导入cdi中

启动虚拟机用的是kubevirt组件，里面一个虚拟机的相关资源有 cdi、vm、vmi和pod，其中

cdi是用来导入虚拟机镜像的，可以用http的方式导入上面打包好的win11的镜像

vm可以理解为定义虚拟机的xml

vmi就是对应的虚拟机运行时的实例

pod就是对应k8s中的运行资源

## bios配置

其中需要额外配置一下vm，因为使用的镜像为win11要使用uef启动，kubevirt默认使用的是biso启动的

![](./boot.png)

## cpu配置

还有就是要注意cpu的相关配置，不然任务管理中的cpu数量会跟虚拟机配置和设备管理器中显示的数量不一致

![](./cpu1.png)

不一致的情况如下

![](./cpu2.png)

## cloudbase配置

kubevirt是支持 `cloudInitNoCloud` 和 `cloudInitConfigDrive`的配置的，并且也支持从k8s的secret中获取配置信息

具体看如下链接

[https://kubevirt.io/user-guide/user_workloads/startup_scripts/#cloud-init-examples](https://kubevirt.io/user-guide/user_workloads/startup_scripts/#cloud-init-examples)

其它参考：

[https://kubevirt.io/2022/KubeVirt-installing_Microsoft_Windows_11_from_an_iso.html](https://kubevirt.io/2022/KubeVirt-installing_Microsoft_Windows_11_from_an_iso.html)

至此，就可以在k8s中解锁win11虚拟机了🥰

