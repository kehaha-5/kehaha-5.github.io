---
title: K8s中VPC-NAT-GW公网IP无法访问的排障过程
date: 2025-05-15 22:45:59
tags: [k8s,kube-ovn,排障]
---

> 测试团队发现在某个环境中，发现公网ip无法正常使用

# 排查

该k8s中使用kube-ovn作为cni，并且对特定的vpc使用集中式网关，该问题就是出现在某个vpc的网关中

[VPC 入门 - Kube-OVN 文档](https://kubeovn.github.io/docs/v1.13.x/vpc/vpc/#vpc_2)

进入对应的vpc-nat-gw的pod，查看信息是否正常

查看网卡信息

![ip-a](./ip-a.png)

查看iptable配置

![ip-a](./iptable-legacy-save.jpg)

查看路由配置

![ip-r](./ip-r.jpg)

上述都没有问题，开始使用tcpdump进行抓包

![ip-r](./tcpdump.jpg)

可用看到有流量从eth0过来（在对应vpc上对应的subnet的应用上面ping 119.29.29.29）

也有xxx.xxx.255.5的流量过来（本地ping对应的公网ip）

上述说明应用流量和公网流量可以正常达到vpc-nat-gw

但是有个问题，按照iptables的配置来说，10.0.4.10的流量应该回经过snat变成公网ip(xxx.xxx.xxx.71)，然后从net1中流出，同理公网ip(xxx.xxx.xxx.71)也应该也要变成10.0.4.10从eth0中流出

所以问题可能出在iptable的配置中

```bash
# 查看POSTROUTING和PREROUTING的规则，看看是否有规则冲突，可以发现规则没有冲突
# iptables-legacy -t nat -L POSTROUTING -n -v 
Chain POSTROUTING (policy ACCEPT 835 packets, 38717 bytes)
 pkts bytes target     prot opt in     out     source               destination         
  0     0   SNAT_FILTER  0    --  *      *       0.0.0.0/0            0.0.0.0/0  
# iptables-legacy -t nat -L PREROUTING  -n -v 
Chain PREROUTING (policy ACCEPT 1189 packets, 60269 bytes)
 pkts bytes target     prot opt in     out     source               destination         
   0    0   DNAT_FILTER  0    --  *      *       0.0.0.0/0            0.0.0.0/0 
   
# 查看dnat和snat的转换记录，发现没有转换数据
# iptables-legacy -t nat -L EXCLUSIVE_SNAT -n -v 
Chain EXCLUSIVE_SNAT (1 references)
 pkts bytes target     prot opt in     out     source               destination         
   0  0 	 SNAT       0    --  *      *       10.0.4.10            0.0.0.0/0          to:xxx.xxx.xxx.71
# iptables-legacy -t nat -L EXCLUSIVE_DNAT -n -v 
Chain EXCLUSIVE_DNAT (1 references)
 pkts bytes target     prot opt in     out     source               destination         
  0 	0    DNAT       0    --  *      *       0.0.0.0/0           xxx.xxx.xxx.71       to:10.0.4.10
```

查看ipv4的转发情况，发现没有开启对于的配置，使用 sysctl -w net.ipv4.ip_forward=1 临时开启，访问就正常了

![ip-r](./net.png)

# 总结

后面发现是对应被调度到vpc-nat-gw的节点没有配置ipv4转发

可能是之前临时配置了（ sysctl -w net.ipv4.ip_forward=1 ），后面被重置了

后续通过编辑 /etc/sysctl.conf 添加 net.ipv4.ip_forward=1进行永久配置