sysctl -w net.ipv4.ip_forward=1

ip link add name test_mtu type bridge
ip link set test_mtu up 
ip addr add 192.168.200.1/24 dev test_mtu 

ip netns add vm_ns
ip link add veth_vm type veth peer name veth_test_mtu
# 创建虚拟机的网桥
ip netns exec vm_ns ip link add name vm_eth0 type bridge
ip netns exec vm_ns ip link set vm_eth0 up
# 将 veth_vm 分配给虚拟机命名空间
ip link set veth_vm netns vm_ns
#  将 veth_vm 添加到虚拟机的网桥 vm_eth0
ip netns exec vm_ns ip link set veth_vm master vm_eth0
ip netns exec vm_ns ip link set veth_vm up
# 配置虚拟机网桥的 IP 地址
ip netns exec vm_ns ip addr add 192.168.200.100/24 dev vm_eth0
# 设置虚拟机的默认路由
ip netns exec vm_ns ip route add default via 192.168.200.1
ip netns exec vm_ns sysctl -w net.ipv4.ip_forward=1
# 把虚拟机的网桥 vm_eth0 添加到 test_mtu 桥接中
ip link set veth_test_mtu master test_mtu
ip link set veth_test_mtu up


# iptables
iptables -t nat -L POSTROUTING --line-numbers
iptables -t nat -D POSTROUTING 1
# snat 
iptables -t nat -A POSTROUTING -s 192.168.200.0/24 -o enp1s0 -j MASQUERADE
iptables -A FORWARD -i test_mtu -o enp1s0 -j ACCEPT
iptables -A FORWARD -i enp1s0 -o test_mtu -m state --state RELATED,ESTABLISHED -j ACCEPT
# 删除
iptables -t nat -D POSTROUTING -s 192.168.200.0/24 -o enp1s0 -j MASQUERADE
iptables -D FORWARD -i br0 -o veth_br -j ACCEPT
iptables -D FORWARD -i veth_br -o br0 -m state --state RELATED,ESTABLISHED -j ACCEPT

# 测试snat
ip netns exec vm_ns ping -c 3 8.8.8.8

#设置mtu
ip link set dev test_mtu mtu 9000
ip netns exec vm_ns ip link set dev vm_eth0 mtu 9000
ip link set veth_test_mtu mtu 9000
ip netns exec vm_ns ip link set dev veth_vm mtu 9000

#测试mtu
ip netns exec vm_ns ping -M do -s 1500 192.168.200.1