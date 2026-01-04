---
title: 虚拟化初探--libvirt+OVN实践
date: 2026-01-04 18:43:47
tags: [OVN,libvirt,边做边学]
---

# 背景

> 最近因为业务需求，需要在新环境部署一套系统用于POC演示。分配的资源是机房中的3台裸金属物理机，要求在上面根据系统和业务需求完成网络虚拟化配置。

### 需求

以下是物理机网络信息

![](./nodes.png)

#### 网络需求

经过梳理，网络虚拟化的需求如下：

1. 要创建两个NAT网络，网络ip为静态分配，确保应用正常部署
2. 一个物理段网络，打通机房其他设备
3. <font style="color:rgb(29, 29, 31);">部分NAT网络中的虚拟机需要做物理网络映射，确保应用正常访问</font>
4. <font style="color:rgb(29, 29, 31);">部分虚拟机因业务要求，还需要桥接物理机上的其他业务网卡</font>

根据上述要求，我们这边选择OVN + OVS 的方式来实现网络虚拟化

> <font style="font-size:14px">**OVS（Open vSwitch）**<font style="color:rgb(29, 29, 31);"> 是一个开源的</font>**虚拟交换机**，用于在虚拟化环境中提供交换功能；</font>
>
> <font style="font-size:14px">**OVN（Open Virtual Network）**是一个基于 OVS 构建的**开源网络虚拟化解决方案**，提供完整的 SDN（软件定义网络）能力，如逻辑交换、路由、ACL 和负载均衡，用于构建多租户虚拟网络。</font>
>
> <font style="font-size:14px">**OVN 是构建在 OVS 之上的网络虚拟化层，OVS 是 OVN 的底层数据面执行引擎**。</font>

#### 虚拟机需求

<font style="color:rgb(29, 29, 31);">虚拟机部署使用libvirtd，因为它能方便地与OVS集成，同时如果不熟悉命令行，还可以通过cockpit等Web界面进行操作。</font>

# 基础配置

### 软件安装

<font style="color:rgb(29, 29, 31);">当前环境是CentOS 9，这里只详细说明ovn的安装方式，其他ovs和libvirtd组件用dnf安装一般不会遇到太多问题：</font>

```bash
# 安装源
dnf config-manager --set-enabled crb
dnf install -y epel-release
dnf install -y centos-release-nfv-openvswitch 

# 搜索ovn相关版本
dnf search openvswitch3

# 这里安装3.4版本的
dnf install -y  openvswitch3.4  ovn24.09-central ovn24.09-host ovn24.09 
```

### 系统配置

```bash
# 物理机都要开启ipv4转发功能
echo "net.ipv4.ip_forward=1" | tee -a /etc/sysctl.conf
sysctl -p
```

# OVN集群配置

<font style="color:rgb(29, 29, 31);">先在每台物理机上启动对应的服务</font>

```bash
systemctl enable --now openvswitch
systemctl enable --now ovn-northd
systemctl enable --now ovn-controller
systemctl enable --now ovs-vswitchd
```

### OVN配置

OVN配置中，数据库配置是关键。OVN包含北向和南向数据库：

> <font style="font-size:14px">北向数据库（NB DB）存储用户定义的逻辑网络配置（如虚拟交换机/路由器）</font>
>
> <font style="font-size:14px">南向数据库（SB DB）存储由`ovn-northd` 生成的、指导OVS执行数据转发的具体流表和物理网络状态</font>

<font style="color:rgb(29, 29, 31);">因为是POC环境，我们只在node1上配置数据库。如果是生产环境，至少需要在3台服务器上配置数据库集群</font>

```bash
# node-1节点设置北向数据库监听
ovn-nbctl set-connection ptcp:6641:node-1IP
ovn-nbctl set connection . inactivity_probe=60000

# 设置南向数据库监听
ovn-sbctl set-connection ptcp:6642:node-1IP
ovn-sbctl set connection . inactivity_probe=60000

# 其他节点要把如下变量配置到 ~/.bashrc中，确保ovn命令正常使用
export OVN_NB_DB="tcp:node-1IP:6641"
export OVN_SB_DB="tcp:node-1IP:6642"
export OVN_NORTHD_SOCK="/var/run/ovn/ovnnb_db.sock"
```

### OVS配置

<font style="color:rgb(29, 29, 31);">OVS配置主要是将物理网卡和业务网卡桥接到OVS中。</font>**<font style="color:rgb(29, 29, 31);">注意</font>**<font style="color:rgb(29, 29, 31);">：在桥接物理网卡时，会导致物理机网络暂时中断，建议通过带外管理执行，或者使用带有恢复功能的脚本。</font>

```bash
# 桥接物理机网卡，物理机网卡为ens1f0np0
ovs-vsctl add-br br-mgmt
ovs-vsctl add-port br-mgmt ens1f0np0
# 刷新物理网卡的IP地址和路由
ip addr flush dev ens1f0np0
ip addr add 192.168.58.100/24 dev br-mgmt
ip link set br-mgmt up
# 这一步根据每个环境的路由配置而定，我这里是192.168.58.0/24
ip route replace default via 192.168.58.254 dev br-mgmt
ip route replace 192.168.58.0/24 dev br-mgmt scope link src 192.168.58.100

# 桥接业务网卡
ovs-vsctl add-br br-test
ovs-vsctl add-port br-mgmt ens1
# 配置对应业务网卡路由，跟上述配置一致

# 持久化
# 创建br-mgmt网桥配置文件
cat > /etc/sysconfig/network-scripts/ifcfg-br-mgmt << EOF
DEVICE=br-mgmt
ONBOOT=yes
DEVICETYPE=ovs
TYPE=OVSBridge
BOOTPROTO=static
IPADDR=192.168.58.100
NETMASK=255.255.255.0
GATEWAY=192.168.58.254
DNS1=119.29.29.29
OVS_EXTRA="set bridge br-mgmt datapath_type=system"
NM_CONTROLLED=no
EOF

# 创建物理网卡ens1f0np0配置文件（桥接到br-mgmt）
cat > /etc/sysconfig/network-scripts/ifcfg-ens1f0np0 << EOF
DEVICE=ens1f0np0
ONBOOT=yes
DEVICETYPE=ovs
TYPE=OVSPort
OVS_BRIDGE=br-mgmt
BOOTPROTO=none
NM_CONTROLLED=no
EOF

# 创建br-test网桥配置文件
cat > /etc/sysconfig/network-scripts/ifcfg-br-test << EOF
DEVICE=br-test
ONBOOT=yes
DEVICETYPE=ovs
TYPE=OVSBridge
BOOTPROTO=none
OVS_EXTRA="set bridge br-test datapath_type=system"
NM_CONTROLLED=no
EOF

# 创建业务网卡ens1配置文件（桥接到br-test）
cat > /etc/sysconfig/network-scripts/ifcfg-ens1 << EOF
DEVICE=ens1
ONBOOT=yes
DEVICETYPE=ovs
TYPE=OVSPort
OVS_BRIDGE=br-test
BOOTPROTO=none
NM_CONTROLLED=no
EOF
```

### OVS连接到OVN

在上面对OVN和OVS的说明中，可以得知OVN是一个控制层，可以通过相关命令添加路由器、交换机等网络组件，而OVS是数据平面，负责OVN中的路由器、交换机中相关网络规则的执行。<font style="color:rgb(29, 29, 31);">因此OVS需要连接到OVN的数据库：</font>

```bash
# 所有物理机的OVS配置external_ids
ovs-vsctl set Open_vSwitch . external_ids:ovn-remote=tcp:node-1IP:6642
ovs-vsctl set Open_vSwitch . external_ids:ovn-nb=tcp:node-1IP:6641
ovs-vsctl set Open_vSwitch . external_ids:ovn-encap-type=geneve
ovs-vsctl set Open_vSwitch . external_ids:ovn-encap-ip=该node自己的ip
ovs-vsctl set Open_vSwitch . external_ids:system-id=$(hostname)
# 对应的物理网卡需要用ovn-bridge-mappings=physnet来做映射
ovs-vsctl set Open_vSwitch . external_ids:ovn-bridge-mappings=physnet:br-mgmt
```

### 配置检查

在上述配置好后，可以使用如下命令来检查

```bash
# 查看节点ovn节点信息
ovn-sbctl show 
Chassis "8db5be44-14d4-4073-8e38-6040c0412837"
    hostname: node-1
    Encap geneve
        ip: "192.168.58.100"
        options: {csum="true"}
Chassis "f4cd7cc3-cbbe-4225-8b6d-8b19f51667ff"
    hostname: node-2
    Encap geneve
        ip: "192.168.58.101"
        options: {csum="true"}
....

# 查看节点上面ovs配置信息
ovs-vsctl show 
9adf5989-96b2-4c92-abef-ced147e20d9f
    Bridge br-int
        fail_mode: secure
        datapath_type: system
        Port ovn-8db5be-0
            Interface ovn-8db5be-0
                type: geneve
                options: {csum="true", key=flow, local_ip="192.168.58.100", remote_ip="192.168.58.100"}
    Bridge br-test
        Port br-test
            Interface br-test
                type: internal
        Port ens1
            Interface ens1
    Bridge br-mgmt
        Port br-mgmt
        Interface br-mgmt
            type: internal
        Port ens1f0np0
            Interface ens1f0np0
    ovs_version: "3.4.4-99.el9s"
```

# 虚拟化网络

根据上述网络要求，给出如下网络架构图

+ <font style="color:rgb(29, 29, 31);">两个NAT网络通过gw-router的SNAT规则实现公网访问</font>
+ NAT网络中的虚拟机可以通过`ovn-nbctl lr-nat-add gw-router dnat_and_snat`<font style="color:rgb(29, 29, 31);">命令进行物理IP映射</font>


![](./architectural.png)

### 交换机和路由器配置

在任意节点上执行ovn命令创建对应的逻辑交换机和逻辑路由器

```bash
# 创建逻辑交换机
ovn-nbctl ls-add nat-switch-1
ovn-nbctl ls-add nat-switch-2
ovn-nbctl ls-add phys-ls
# 创建逻辑路由器
ovn-nbctl lr-add gw-router
```

把交换机连接到路由器

```bash
# nat-switch-1连接路由器
ovn-nbctl lsp-add nat-switch-1 ls-switch-1-gw
ovn-nbctl lsp-set-type ls-switch-1-gw router
ovn-nbctl lsp-set-addresses ls-switch-1-gw router
ovn-nbctl lsp-set-options ls-switch-1-gw router-port=gw-switch-1
ovn-nbctl lrp-add gw-router gw-switch-1 00:00:00:00:00:03 10.140.0.254/24

# nat-switch-2连接路由器
ovn-nbctl lsp-add nat-switch-2 ls-switch-2-gw
ovn-nbctl lsp-set-type ls-switch-2-gw router
ovn-nbctl lsp-set-addresses ls-switch-2-gw router
ovn-nbctl lsp-set-options ls-switch-2-gw router-port=gw-switch-2
ovn-nbctl lrp-add gw-router gw-switch-2 00:00:00:00:00:04 10.141.0.254/24

# phsy-ls连接路由器
ovn-nbctl lsp-add phys-ls ls-phys-gw
ovn-nbctl lsp-set-type ls-phys-gw router
ovn-nbctl lsp-set-addresses ls-phys-gw router
ovn-nbctl lsp-set-options ls-phys-gw router-port=gw-phys
ovn-nbctl lrp-add gw-router gw-phys 00:00:00:00:00:05 192.168.58.103/24
```

路由器路由配置

```bash
# 配置NAT规则
ovn-nbctl lr-nat-add gw-router snat 192.168.58.104 10.140.0/24
ovn-nbctl lr-nat-add gw-router snat 192.168.58.104 10.141.0.0/24
# 配置默认路由
ovn-nbctl lr-route-add gw-router 0.0.0.0/0 192.168.58.254
```

### 物理网络打通配置

在逻辑交换机上面创建物理网络端口，确保流量可以正常走到物理网关

```bash
# 在物理交换机上面添加端口
ovn-nbctl lsp-add phys-ls phys-port
# 设置端口类型为localnet（连接到物理网络）
ovn-nbctl lsp-set-type phys-port localnet
network_name的值应与OVS桥接映射中的物理网络名称(physnet)一致 
ovn-nbctl lsp-set-options phys-port network_name=physnet
ovn-nbctl lsp-set-addresses phys-port unknown
```

### 配置检查

```bash
# 查看ovn南向数据库
ovn-nbctl show
switch d33268b0-8222-4bec-b553-ae0485797511 (nat-switch-2)
    port ls-switch-2-gw
        type: router
        router-port: gw-switch-2
switch 7e008646-18cd-4bbb-894e-e5cd213951c4 (phys-ls)
    port ls-phys-gw
        type: router
        router-port: gw-phys
    port phys-port
        type: localnet
        addresses: ["unknown"]
switch d5d21bc3-5482-4f51-8198-ea114784a4c8 (nat-switch-1)
    port ls-switch-1-gw
        type: router
        router-port: gw-switch-1
router 583a20f7-0d34-465b-869a-60e4ff94ed51 (gw-router)
    port gw-switch-1
        mac: "00:00:00:00:00:03"
        ipv6-lla: "fe80::200:ff:fe00:3"
        networks: ["10.140.254/24"]
    port gw-switch-2
        mac: "00:00:00:00:00:04"
        ipv6-lla: "fe80::200:ff:fe00:4"
        networks: ["10.141.254/24"]
    port gw-phys
        mac: "00:00:00:00:00:05"
        ipv6-lla: "fe80::200:ff:fe00:5"
        networks: ["192.168.58.103/24"]
    nat 4454374b-7457-4fd4-947f-5e639985eeb6
        external ip: "10.24.250.129"
        logical ip: "10.140.0.0/24"
        type: "snat"
    nat b5312d44-3102-450f-a935-638d11e4a2eb
        external ip: "10.24.250.129"
        logical ip: "10.141.0.0/24"
        type: "snat"

# 查看ovs具体配置
ovs-vsctl show 
9adf5989-96b2-4c92-abef-ced147e20d9f
    Bridge br-int
        fail_mode: secure
        datapath_type: system
        Port ovn-8db5be-0
            Interface ovn-8db5be-0
                type: geneve
                options: {csum="true", key=flow, local_ip="192.168.58.100", remote_ip="192.168.58.100"}
        ....
        # 在添加物理网络卡后，ovs会创建patch确保物理网正常访问
        Port patch-br-int-to-phys-port
            Interface patch-br-int-to-phys-port
                type: patch
                options: {peer=patch-phys-port-to-br-int}
    Bridge br-test
        Port br-test
            Interface br-test
                type: internal
        Port ens1
            Interface ens1
    Bridge br-mgmt
        Port ens1f0np0
            Interface ens1f0np0
        Port br-mgmt
            Interface br-mgmt
                type: internal
        # 在添加物理网络卡后，ovs会创建patch确保物理网正常访问
        Port patch-phys-port-to-br-int
            Interface patch-phys-port-to-br-int
                type: patch
                options: {peer=patch-br-int-to-phys-port}
    ovs_version: "3.4.4-99.el9s"

# 逻辑路由器信息 
ovn-nbctl show gw-router
# NAT规则 
ovn-nbctl lr-nat-list gw-router
# 逻辑交换机信息 
ovn-nbctl show phys-ls
```



# libvirt虚拟机相关配置

在上述网络配置好后，我们要创建虚拟机。<font style="color:rgb(29, 29, 31);">如果每次都要到引导界面手动安装系统会很麻烦，这里我们使用每个镜像的cloud-image版本，可以跳过系统安装过程。</font>

我们使用`CentOS-Stream-GenericCloud-9-20250812.1.x86_64.qcow2`<font style="color:rgb(29, 29, 31);">镜像进行配置</font>

> <font style="font-size:14px">Cloud-image版本是专为云平台优化的虚拟机镜像，内置cloud-init工具以支持启动时的自动化初始化（如网络配置、SSH密钥注入）</font>

### cloud-image配置

cloud-image一般是qcow2格式，下载好后虽然可以直接启动，<font style="color:rgb(29, 29, 31);">但存在一些问题：不知道root密码、无法登录系统、根目录空间很小等</font>

为了解决上述问题，需要提前进行配置

#### 磁盘扩容

```bash
# 使用qemu-img合适大小的磁盘
qemu-img create -f qcow2 ./test.qcow2 100G
# 使用virt-filesystems查看镜像具体磁盘信息
virt-filesystems --partitions --long -a  ./CentOS-Stream-GenericCloud-9-20250812.1.x86_64.qcow2 
Name       Type       MBR  Size        Parent
/dev/sda1  partition  83   8388608000  /dev/sda
# 这里要对/dev/sda1进行分区扩容
virt-resize --expand /dev/sda1 \
 ./CentOS-Stream-GenericCloud-9-20250812.1.x86_64.qcow2 \
  ./test.qcow2
...
```

等待扩容命令执行完毕，再使用test.qcow2创建的虚拟机，磁盘大小就正常了

#### 初始密码修改

这里修改密码使用了cloud-init

> <font style="font-size:14px">**cloud-init 是 Linux 云实例的**自动化初始化工具，通过云平台提供的元数据（如 SSH 密钥、网络配置）在首次启动时动态完成系统配置。</font>

这里使用的是<font style="color:rgb(0, 0, 0);">NoCloudConfigDriveService，即创建对应的iso文件挂载到虚拟机中，就是执行iso中对应的配置</font>

<font style="color:rgb(0, 0, 0);">这里要创建meta-data和user-data两个文件，其中</font>

+ meta-data用来定义虚拟机的基础元信息（如实例ID、主机名、网络拓扑）
+ user-data用来提供用户自定义的初始化指令如创建用户、注入SSH密钥、运行脚本）

```yaml
cat ./meta-data 
# 确保instance-id没有改变，如instance-id改变了，虚拟机启动时会重新执行user-data中的命令
instance-id: localhost
local-hostname: ovn-vm

cat ./user-data
# cloud-config
chpasswd:
  expire: false
  users:
  - {name: root, password: xxxx, type: text}
ssh_pwauth: true
```

在创建好上述文件后，使用如下命令打打包成iso并验证

```yaml
# 创建ISO镜像
genisoimage -output cloud-init.iso -volid cidata -joliet -rock iso/

# 验证ISO
isoinfo -d -i cloud-init.iso
```

<font style="color:rgb(29, 29, 31);">在虚拟机启动时，将这个ISO挂载进去即可。</font>

# libvirt接入OVN网络

<font style="color:rgb(29, 29, 31);">OVN网络配置完成后，需要将虚拟机连接到对应的交换机端口。</font>

### 网络创建

在libvirtd中创建对应的网络

```xml
# nat虚拟机网络
<network>
  <name>nat-net</name>
  <forward mode='bridge'/>
  <bridge name='br-int'/>
  <virtualport type='openvswitch'/>
  <portgroup name='ovn-nat' default='yes'>
    <virtualport />
  </portgroup>
</network>

# 物理网段
<network>
  <name>phys-net</name>
  <forward mode='bridge'/>
  <bridge name='br-mgmt'/>
  <virtualport type='openvswitch'/>
</network>

# 业务网卡网段
<network>
  <name>test-net</name>
  <forward mode='bridge'/>
  <bridge name='br-test'/>
  <virtualport type='openvswitch'/>
</network>
```

执行以下命令创建网络

```bash
virsh net defind ./xx.xml
virsh net start xxx
virsh net auto-start xxx
# 其他网络同理
```

### 虚拟机创建

在创建虚拟机的时候，要添加对应的network到虚拟机里面，这里以nat网络的虚拟机为例

这里使用xml创建，确保xml中的网卡配置如下

```xml
<domain type='kvm'>
  <name>xxx</name>
  <memory unit='GiB'>8</memory>
  <vcpu>8</vcpu>
  <os>
    <type arch='x86_64'>hvm</type>
    <boot dev='hd'/>
    </os>
  <cpu mode='host-passthrough'>
    <model fallback='allow'/>
    </cpu>
  <devices>
    <disk type='file' device='disk'>
      <driver name='qemu' type='qcow2'/>
      <source file='/data/vm/xxx.qcow2'/>
      <target dev='vda' bus='virtio'/>
    </disk>
    <!-- cloud-init ISO -->
    <disk type='file' device='cdrom'>
      <driver name='qemu' type='raw'/>
        <source file='/data/vm/cloud-init/cloud-init.iso'/>
        <target dev='hda' bus='ide'/>
          <readonly/>
        <address type='drive' controller='0' bus='0' target='0' unit='0'/>
    </disk>
    <!-- 确保网络配置如下 --> 
    <interface type='network'>
      <source network='nat-net'/>
        <model type='virtio'/>
    </interface>
    <console type='pty'>
      <target type='serial' port='0'/>
    </console>
    <serial type='pty'/>
      <graphics type='vnc' port='-1' autoport='yes'/>
    </devices>
</domain>
```

在虚拟机创建出来后，可以使用如下命令查看其在ovs中的网卡端口

```bash
virsh domiflist --domain test 
 接口    类型     源       型号     MAC
-------------------------------------------------------
 vnet0   bridge   br-int   virtio   52:54:00:90:04:48
ovs-vsctl list interface vnet0 
_uuid               : 6296696a-e99c-42cd-9d8f-723c254237b7
....
external_ids        : {attached-mac="52:54:00:90:04:48", iface-id="652d58bc-8006-4e40-ad31-4786c036aa88", iface-status=active, ovn-installed="true", ovn-installed-ts="1766975969094", vm-id="ffedf175-fb79-4d47-8f76-b785d90545a8"}
```

主要关注`external_ids`中的`iface-id`和`attached-mac`地址：

```bash
ovs-vsctl get interface vnet0 external_ids:iface-id
"652d58bc-8006-4e40-ad31-4786c036aa88"
ovs-vsctl get interface vnet0 external_ids:attached-mac
"52:54:00:90:04:48"
```

用这个iface-id在OVN的对应交换机下创建端口

```bash
ovs-vsctl get interface vnet0  external_ids:iface-id
"652d58bc-8006-4e40-ad31-4786c036aa88"
ovs-vsctl get interface vnet0  external_ids:attached-mac 
"52:54:00:90:04:48"
# 这里在nat-switch-1中创建端口
ovn-nbctl lsp-add nat-switch-1 652d58bc-8006-4e40-ad31-4786c036aa88
ovn-nbctl lsp-set-addresses 652d58bc-8006-4e40-ad31-4786c036aa88 52:54:00:90:04:48 10.140.0.10
# 查看端口状态
ovn-nbctl list Logical_Switch_Port 652d58bc-8006-4e40-ad31-4786c036aa88
_uuid               : dc27568b-c146-4f98-8a55-0891b3013902
addresses           : ["52:54:00:90:04:48 10.143.1.10"]
dhcpv4_options      : []
dhcpv6_options      : []
dynamic_addresses   : []
enabled             : []
external_ids        : {}
ha_chassis_group    : []
mirror_rules        : []
name                : "652d58bc-8006-4e40-ad31-4786c036aa88"
options             : {}
parent_name         : []
port_security       : []
tag                 : []
tag_request         : []
type                : ""
up                  : true
```

当看到`up`字段为`true`时，表示交换机端口和虚拟机成功连接。

接下来进入虚拟机配置对应的ip

```bash
virsh console test 
...
[root@ovn-vm ~]# nmcli connection show
NAME         UUID                                  TYPE      DEVICE 
System eth0  5fb06bd0-0bb0-7ffb-45f1-d6edd65f3e03  ethernet  eth0   
lo           14ebbabb-e590-414d-8e1f-089c12708d77  loopback  lo     
ens3         e9a7d58a-497a-4e32-aad5-19f630b45be3  ethernet  -- 
[root@ovn-vm ~]# nmcli connection modify "System eth0"  ipv4.method manual ipv4.addresses 10.140.0.10/24 ipv4.gateway 10.140.0.254
[root@ovn-vm ~]# nmcli connection modify "System eth0" ipv4.dns 119.29.29.29
[root@ovn-vm ~]# ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
    link/ether 52:54:00:30:49:ff brd ff:ff:ff:ff:ff:ff
    altname enp0s3
    inet 10.140.0.10/24 brd 10.140.0.255 scope global noprefixroute eth0
       valid_lft forever preferred_lft forever
    inet6 fe80::5054:ff:fe30:49ff/64 scope link noprefixroute 
       valid_lft forever preferred_lft forever
[root@ovn-vm ~]# ping 119.29.29.29 -c 3
PING 119.29.29.29 (119.29.29.29) 56(84) bytes of data.
64 bytes from 119.29.29.29: icmp_seq=1 ttl=51 time=76.9 ms
64 bytes from 119.29.29.29: icmp_seq=2 ttl=51 time=76.6 ms
64 bytes from 119.29.29.29: icmp_seq=3 ttl=51 time=76.5 ms

--- 119.29.29.29 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 2004ms
rtt min/avg/max/mdev = 76.516/76.672/76.934/0.186 ms
# [root@vm ~]# ping 10.140.0.254 -c 3
PING 10.140.0.254 (10.140.0.254) 56(84) bytes of data.
64 bytes from 10.140.0.254: icmp_seq=1 ttl=254 time=0.302 ms
64 bytes from 10.140.0.254: icmp_seq=2 ttl=254 time=0.166 ms
64 bytes from 10.140.0.254: icmp_seq=3 ttl=254 time=0.215 ms

--- 10.140.0.254 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 2057ms
rtt min/avg/max/mdev = 0.166/0.227/0.302/0.056 ms
```

### <font style="color:rgb(29, 29, 31);">业务网卡桥接</font>

业务网卡只用在虚拟机创建的xml中配置好对应的`<interface>`即可，后续不需要在OVN中创建端口

### 虚拟机删除

在虚拟机删除后，对应的ovs的port会自己删除，但是ovn上面的要手动删除

```bash
# 对应的iface-id
ovn-nbctl lsp-del 652d58bc-8006-4e40-ad31-4786c036aa88
```

<font style="color:rgb(29, 29, 31);">实际上，可以编写一个libvirt hook脚本来自动化这个过程，</font>这里就不写了，你们可以自行AI一下🧐

# 总结

<font style="color:rgb(29, 29, 31);">目前上述配置适合在POC、UAT等测试环境中使用。如果要在生产环境中使用虚拟化，建议还是使用Proxmox VE等专业虚拟化平台，它们提供了更完善的管理界面、高可用性和资源调度能力。</font>

<font style="color:rgb(29, 29, 31);">不过关键是要理解OVN的控制平面和OVS的数据平面如何协同工作，以及如何通过libvirt将虚拟机无缝接入这个网络。</font>

# <font style="color:rgb(29, 29, 31);">参考文档</font>

libvirt:

[https://www.libvirt.org/hooks.html](https://www.libvirt.org/hooks.html)

[https://cloudinit.readthedocs.io/en/latest/index.html](https://cloudinit.readthedocs.io/en/latest/index.html)

OVS:

[https://docs.openvswitch.org/en/latest/howto/libvirt/](https://docs.openvswitch.org/en/latest/howto/libvirt/)

OVN:

[https://docs.ovn.org/en/latest/tutorials/index.html](https://docs.ovn.org/en/latest/tutorials/index.html)

了解OVN：

[https://www.ducksource.blog/p/sdn-using-ovn-and-openvswitch/](https://www.ducksource.blog/p/sdn-using-ovn-and-openvswitch/)

虚拟机集成OVN：

[https://blog.scottlowe.org/2016/12/09/using-ovn-with-kvm-libvirt/](https://blog.scottlowe.org/2016/12/09/using-ovn-with-kvm-libvirt/)



