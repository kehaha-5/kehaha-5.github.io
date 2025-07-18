---
title: 使用ansible管理windwos
date: 2025-07-06 23:43:10
tags: [windows,ansible,边做边学]
---

> 最近要对20多台windows主机进行管理（启动进程、挂载网络硬盘和修改环境变量），所以使用了ansible做管理工具

# 安装ansible

在使用absible时，只需要在集群中的一台选择任意一台服务器(linux系统)在上面安装ansible即可

但是集群中是windwos系统， 所以要在windwos上面安装wsl，先要在系统功能里面开启wsl功能，然后安装wsl2程序

[安装 WSL | Microsoft Learn](https://learn.microsoft.com/zh-cn/windows/wsl/install)

```powershell
wsl --list --online # 查询可以按照的linux版本
wsl --install -d <Distribution Name> # 安装对应的linux系统
```

安装完成后在wsl中安装ansible，我的系统是使用dnf进行安装

```shell
# dnf install ansible-core
```

# 配置windwos

ansible要控制windwos的话，windwos需要进行配置，要开启windwos的wrm（Windows Remote Management）或者配置ssh服务

[https://docs.ansible.com/ansible/latest/os_guide/windows_winrm.html](https://docs.ansible.com/ansible/latest/os_guide/windows_winrm.html)

[https://docs.ansible.com/ansible/latest/os_guide/windows_ssh.html](https://docs.ansible.com/ansible/latest/os_guide/windows_ssh.html)

这里配置ssh，因为它功能更好，但是要注意版本

> <font style="color:rgb(64, 64, 64);background-color:rgb(252, 252, 252);">Microsoft provides an OpenSSH implementation with Windows since Windows Server 2019 as a Windows capability. It can also be installed through an upstream package under </font>[<font style="color:rgb(41, 128, 185);">Win32-OpenSSH</font>](https://github.com/PowerShell/Win32-OpenSSH)<font style="color:rgb(64, 64, 64);background-color:rgb(252, 252, 252);">. Ansible officially only supports the OpenSSH implementation shipped with Windows, not the upstream package. The OpenSSH version must be version </font> `7.9.0.0` <font style="color:rgb(64, 64, 64);background-color:rgb(252, 252, 252);"> at a minimum. This effectively means official support starts with Windows Server 2022 because Server 2019 ships with version `7.7.2.1` <font style="color:rgb(64, 64, 64);background-color:rgb(252, 252, 252);">. Using older Windows versions or the upstream package might work but is not supported.</font>

<font style="color:rgb(64, 64, 64);background-color:rgb(252, 252, 252);">配置ssh的话可以根据文档进行安装</font>

```powershell
Get-WindowsCapability -Name OpenSSH.Server* -Online |
    Add-WindowsCapability -Online
Set-Service -Name sshd -StartupType Automatic -Status Running

$firewallParams = @{
    Name        = 'sshd-Server-In-TCP'
    DisplayName = 'Inbound rule for OpenSSH Server (sshd) on TCP port 22'
    Action      = 'Allow'
    Direction   = 'Inbound'
    Enabled     = 'True'  # This is not a boolean but an enum
    Profile     = 'Any'
    Protocol    = 'TCP'
    LocalPort   = 22
}
New-NetFirewallRule @firewallParams

$shellParams = @{
    Path         = 'HKLM:\SOFTWARE\OpenSSH'
    Name         = 'DefaultShell'
    Value        = 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
    PropertyType = 'String'
    Force        = $true
}
New-ItemProperty @shellParams
```

指定登录后的shell为powershell

```powershell
# Set default to powershell.exe
$shellParams = @{
    Path         = 'HKLM:\SOFTWARE\OpenSSH'
    Name         = 'DefaultShell'
    Value        = 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
    PropertyType = 'String'
    Force        = $true
}
New-ItemProperty @shellParams

# Set default back to cmd.exe
Remove-ItemProperty -Path HKLM:\SOFTWARE\OpenSSH -Name DefaultShell
```

配置好后，可以通过ssh windows用户名@地址 登录到系统

# 配置ansible

首先要对我们将要管理的资产写一个ansible的资产文件里面

```powershell
win:
  hosts:
   10.88.128.xxx:
   10.88.128.xxx:
  vars:
    ansible_user: "xxx" # Windows上的SSH用户名
    ansible_password: "xxx" # Windows SSH用户的密码 (或者配置SSH Key)
    ansible_port: 22 # SSH默认端口
    ansible_connection: ssh # 指定使用SSH连接
    ansible_shell_type: powershell # 指定Windows上的Shell类型，Powershell是常见的选择
```

配置好后可以进行测试

```shell
[root@Server-68ca6f1e-6023-4e99-9051-10e3746df9bb ~]# ansible -i /etc/ansible/win_inventory.yml all -m win_ping 
[WARNING]: Collection ansible.windows does not support Ansible version 2.14.18
[WARNING]: Collection ansible.windows does not support Ansible version 2.14.18
[WARNING]: Collection ansible.windows does not support Ansible version 2.14.18
[WARNING]: Collection ansible.windows does not support Ansible version 2.14.18
[WARNING]: Collection ansible.windows does not support Ansible version 2.14.18
10.88.128.xx | SUCCESS => {
    "changed": false,
    "ping": "pong"
}
[WARNING]: Collection ansible.windows does not support Ansible version 2.14.18
10.88.128.xxx | SUCCESS => {
    "changed": false,
    "ping": "pong"
}
```

如果报win模块没有的话要进行安装

```shell
ansible-galaxy collection install ansible.windows
```

如果使用win_pong发现结果是乱码的话，可以到windows上面重新安装一下ssh

```shell
Remove-WindowsCapability -Online -Name OpenSSH.Client # 卸载open-ssh
# 重新执行上面的脚本安装open-ssh
```

后面就可以根据实际情况编写playbook了

ansible中win相关的命令文档

[https://docs.ansible.com/ansible/latest/collections/ansible/windows/index.html#plugins-in-ansible-windows](https://docs.ansible.com/ansible/latest/collections/ansible/windows/index.html#plugins-in-ansible-windows)

