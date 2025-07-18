---
title: k8s中为Pod配置自签证书以访问HTTPS服务
date: 2025-03-22 10:00:19
tags: [k8s,certs,边做边学]
---

> 最近在部署一套系统的其它环境时，遇到了自签https证书和应用自签证书到k8s中的问题，故此记录一下。
>
> 在部署的应用中，有一个需要使用Standard OIDC Client所有必须要https，而且要认证的服务不是部署在本集群中

# 概念

## Https相关概念

什么是https，https证书的种类，如何自签https这些内容这里就不做展开了仅作一个自己的总结。因为网上的文章和视频太多了，这里推荐一下几位大佬的文章。

* [有关 TLS/SSL 证书的一切 | 卡瓦邦噶！](https://www.kawabangga.com/posts/5330)
* [HTTPS 隐私安全的一些实践](https://blog.laisky.com/p/https-in-action/#gsc.tab=0)
* [写给开发人员的实用密码学（八）—— 数字证书与 TLS 协议 - This Cute World](https://thiscute.world/posts/about-tls-cert/)

## 总结

**以下总结部分摘抄与上述文章中**

- **HTTPS是HTTP+TLS**，其中的TLS就是为了向客户端证明服务端自己的身份，避免别人伪造自己的身份对客户端造成影响。
- **CA、客户端、网站**之间的关系
  - 客户端信任 CA 机构；
  - CA 机构给网站签发证书；
  - 客户端在访问网站的时候，网站出示自己的证书，由于客户端信任 CA 机构，也就信任 CA 机构签发的证书；
- PKI 架构使用「**数字证书链**（也叫做**信任链**）」的机制来解决这个问题: 
  - CA 机构首先生成自己的根证书与私钥，并使用私钥给根证书签名
    - 因为私钥跟证书本身就是一对，因此根证书也被称作「自签名证书」
  - CA 根证书被直接交付给各大软硬件厂商，内置在主流的操作系统与浏览器中
  - 然后 CA 机构再使用私钥签发一些所谓的「中间证书」，之后就把私钥雪藏了，非必要不会再拿出来使用。
    - 根证书的私钥通常**离线存储**在安全地点
    - 中间证书的有效期通常会比根证书短一些
    - 部分中间证书会被作为备份使用，平常不会启用
  - CA 机构使用这些中间证书的私钥，为用户提交的所有 CSR 请求签名
  - CA 机构也可能会在经过严格审核后，为其他机构签发中间证书，这样就能赋予其他机构签发证书的权利，而且根证书的安全性不受影响。
  - 客户端要相信这个证书，最终的目的一定是验证到一个自己信任的 **CA Root** 上去。一旦验证了 **CA Root**，客户端就信任了 **Root** 签发的证书，也就信任了他们的证书。
- 交叉签名
  - 实际上在 PKI 体系中，一些证书链上的中间证书会被使用多个根证书进行签名——我们称这为交叉签名。交叉签名的主要目的是提升证书的兼容性——客户端只要安装有其中任何一个根证书，就能正常验证这个中间证书。
  - 从而使中间证书在较老的设备也能顺利通过证书验证。
- 证书状态认证 **OCSP协议**
  - 为了在私钥泄漏的情况下，能够吊销对应的证书，PKI 公钥基础设施还提供了 OCSP（Online Certificate Status Protocol）证书状态查询协议。
  - Chrome/Firefox 等浏览器都会定期通过 OCSP 协议去请求 CA 机构的 OCSP 服务器验证证书状态， 这可能会拖慢 HTTPS 协议的响应速度。
  - 因为客户端直接去请求 CA 机构的 OCSP 地址获取证书状态，这就导致 CA 机构可以获取到一些对应站点的用户信息（IP 地址、网络状态等）。
  - 为了解决这两个问题，[rfc6066](https://www.rfc-editor.org/rfc/rfc6066) 定义了 OCSP stapling 功能，它使服务器可以提前访问 OCSP 获取证书状态信息并缓存到本地，基本 Nginx/Caddy 等各大 Web 服务器或网关，都支持 OCSP stapling 协议。

# 实践

## 证书的生成和安装

### 生成自签证书

流程 

- 生成私钥 key
- 生成**证书生成请求csr**，可以在**csr**中配置要生成的证书的信息，如**countryName、stateOrProvinceName、organizationName、CA、SAN**
  - **SAN (Subject Alternative Name):** 可选字段，用于指定证书可以使用的其他域名或主机名。
    - 可以通过配置这个来让证书保护多个域名，例如 `example.com`、`www.example.com`、`*.example.com`。
    - 可以使用这个配置来对对应的IP进行保护，即把SAN配置为对应的IP地址
  - **CA**：配置为 `TRUE` 或 `FALSE`，它指示该证书是否可以作为证书颁发机构 (CA) 证书。
    - **`CA:TRUE`**：表明该证书可以用于签署其他证书。 也就是说，这个证书可以作为一个 CA 证书，颁发下级证书，用于创建根 CA 证书或中间 CA 证书。 只有 CA 证书才能签署其他证书，建立信任链。
    - **`CA=FALSE`**：表明该证书不能用于签署其他证书。 也就是说，这个证书是一个终端实体证书，只能用于身份验证、加密等目的，不能颁发下级证书，用于创建服务器证书、客户端证书、代码签名证书等。 这些证书用于保护特定的服务或应用程序，而不是用于颁发其他证书。
- 通过`key+csr`生成对应的`crt`证书

本文通过`openssl`来生成自签证书，以下是生成证书的脚本

```shell
#!/bin/bash

# Check if domain name is provided
if [ -z "$1" ]; then
    echo "Usage: $0 <domain>"
    exit 1
fi

DOMAIN=$1
# 证书过期日期
DAYS=3650
COUNTRY="CN"
STATE="GZ"
CERT_DIR="./certs"
CONFIG_FILE="$CERT_DIR/openssl.cnf"

# Create directory for certificates if it doesn't exist
mkdir -p $CERT_DIR

# 生成 private key
openssl genrsa -out $CERT_DIR/${DOMAIN}.key 2048

# Create OpenSSL configuration file 通过该配置文件生成对应的csr
cat > $CONFIG_FILE <<EOL
[ req ]
distinguished_name = req_distinguished_name
prompt = no
default_bits = 2048
req_extensions = cert_ext

[ req_distinguished_name ]
countryName         = $COUNTRY
stateOrProvinceName = $STATE
localityName        = $STATE
organizationName    = $STATE
commonName          = $DOMAIN

[ cert_ext ]
basicConstraints = CA:TRUE # CA配置 
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth, clientAuth
subjectAltName = @alt_names
# SAN配置
[ alt_names ]
DNS.1 = $DOMAIN
DNS.2 = *.$DOMAIN

EOL

# Generate CSR with extensions 生成csr
openssl req -new -key $CERT_DIR/${DOMAIN}.key -out $CERT_DIR/${DOMAIN}.csr -config $CONFIG_FILE

# Generate self-signed certificate with both extensions 生成crt
openssl x509 -req -days $DAYS \
    -in $CERT_DIR/${DOMAIN}.csr \
    -signkey $CERT_DIR/${DOMAIN}.key \
    -out $CERT_DIR/${DOMAIN}.crt \
    -extensions cert_ext \
    -extfile $CONFIG_FILE

echo "Certificate and key have been generated in the $CERT_DIR directory."
```

对应生成出来的证书，可以使用以下命令来进行查看

```shell
# 查看证书(crt)信息
openssl x509 -noout -text -in server.crt

# 查看证书请求(csr)信息
openssl req -noout -text -in server.csr

# 查看 RSA 私钥(key)信息
openssl rsa -noout -text -in server.key

# 验证证书是否可信
## 1. 使用系统的证书链进行验证
openssl verify server.crt
## 2. 使用指定的 CA 证书进行验证
openssl verify -CAfile ca.crt server.crt
```

### 为系统安装自签证书

这里只介绍 RockyLinux9.3 和 Debain12 。因为我的系统是`RockyLinux9.3`，pod的镜像是 `Debain12`其它的大差不差。

RockyLinux9.3

- 为RockyLinux安装自签证书要有一个注意点，既要把证书中的 **CA** 设置为 **TRUE**
- 复制证书到 `/etc/pki/ca-trust/source/anchors/` 执行 `update-ca-trust `
- 就会生成包含私有证书的`ca-bundle.crt`到`/etc/pki/tls/certs/`中

Debain12

- 复制证书到 `/usr/local/share/ca-certificates/` 执行 `update-ca-certificates --fresh`
- 就会生包含私有证书的`ca-certificates.crt` 到 `/etc/ssl/certs/`中

安装完成以后，就可以使用`curl`命令对证书进行验证

```shell
# 在把证书配置到web服务后，可以使用curl命令来查看证书是否匹配
curl -v https://xxx.test.com --cacert /etc/ssl/certs/xxx.test.com.crt 
```

## 为Pod配置证书

- 本次将会使用两种方式来进行配置，一种是通过`init-container`的方式来安装证书，另一种是使用`certs-manager`中的`trust-manager`来自动管理pod的证书

- 应用部署环境：项目使用的是`Kustomize`来部署不同的环境。项目中的代码：[k8s_demo/certs/app at main · kehaha-5/k8s_demo](https://github.com/kehaha-5/k8s_demo/tree/main/certs/app)


```shell
[root@rocky-testing app]# tree -L 3
.
├── debianupcaDockerfile
├── deploy
│   ├── base
│   │   ├── deployment.yaml
│   │   └── kustomization.yaml
│   └── overlays
│       ├── prod # 生成环境用的是CA的证书
│       ├── uat # 使用init-container安装证书
│       └── uat-trust # 使用trust-manager
├── Dockerfile
├── go.mod
├── go.sum
├── main.go
└── makefile

6 directories, 8 files
```

### 在镜像系统中安装证书

根据上述的为系统安装证书的步骤，我们可以使用 `init-container` 把证书安装到应用的`pod`中。注意一些镜像是没有默认安装`ca-certificates ` 需要额外安装或者把物理机的证书`COPY`到镜像内

1. 首先先把证书crt apply到k8s的secret中

```shell
kubectl create secret generic ssl-test-web-crt --from-file=./ssl.test.com.crt --dry-run=client  -o yaml  | kubectl apply -f -
```

2. 使用`init-container` 把证书生成到`ca-certificates.crt`中，同时把新的证书挂载到应用的`Pod`中

```yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization

....

patchesJson6902:
  - target:
      group: apps
      version: v1
      kind: Deployment
      name: https-client
    patch: |-
    .....
      - op: add
      # 把包含证书的secret挂载到volumes中
        path: /spec/template/spec/volumes/-
        value:
          name: ssl-test-web-crt
          secret:
            secretName: ssl-test-web-crt
      - op: add
      # 把生成好的ca-certificates.crt挂载到应用上
        path: /spec/template/spec/containers/0/volumeMounts/-
        value:
          name: ssl-certs
          mountPath: /etc/ssl/certs/
      - op: add
      # 使用emptyDir来临时保存生成的ca-certificates.crt
        path: /spec/template/spec/volumes/-
        value:
          name: ssl-certs
          emptyDir: {}
      - op: add
        path: /spec/template/spec/initContainers
        value:
          - name: init-container
            image: debian
            command: ["/bin/sh", "-c", "update-ca-certificates --fresh && cp -r /etc/ssl/certs/* /mnt/ssl-certs/" ]
            volumeMounts:
              - name: ssl-test-web-crt
                mountPath: /usr/local/share/ca-certificates/
              - name: ssl-certs
                mountPath: /mnt/ssl-certs
```

3. 使用 `kubectl apply -k ./deploy/overlays/uat/`部署该环境

查看日志对应的日志，发现`init-container`生成新证书，并且应用https访问成功

![log](.\uat-log.png)

### 使用trust-manager

在`certs-manager`的文档中有一个叫`trust-manager`的组件[trust-manager - cert-manager Documentation](https://cert-manager.io/docs/trust/trust-manager/)

>trust-manager is the easiest way to manage trust bundles in Kubernetes and OpenShift clusters.
>
>It orchestrates bundles of trusted X.509 certificates which are primarily used for validating certificates during a TLS handshake but can be used in other situations, too.

- 你可以单独安装`trust-manager`，使用里面的`bundles`来配置证书https://cert-manager.io/docs/trust/trust-manager/installation/#installing-trust-manager-without-cert-manager

- 也可以安装`certs-manager`和`trust-manager`，使用`certs-manager`生成自签证书，然后配置到`trust-manager`中 https://cert-manager.io/docs/trust/trust-manager/#quick-start-example

我这里直接只安装`trust-manager`使用`bundles`来配置证书

- 先通过`helm`安装`trust-manager`
  - 注意安装的时候有一个`app.trust.namespace`的配置(默认值为cert-manager)，这个配置定义了那些`namespace`下的`Secret`可以被读取，这样保证了应用的安全 https://cert-manager.io/docs/trust/trust-manager/installation/#trust-namespace

> By default, the trust namespace is the only namespace where`Secret`s will be read. This restriction is in place for security reasons - we don't want to give trust-manager the permission to read all `Secret`s in all namespaces. With additional configuration, secrets may be read from or written to other namespaces.

```yaml
helm repo add jetstack https://charts.jetstack.io --force-update
helm upgrade trust-manager jetstack/trust-manager \
  --install \
  --namespace cert-manager \
  --wait
# 可以利用 helm template 命令生成对应的helm value 
helm template \
  trust-manager jetstack/cert-manager \
  --namespace cert-manager 
  > cert-manager.custom.yaml
```

- 先把证书导入到`trust-namespace`的 

```yaml
kubectl create secret generic ssl-test-web-crt --from-file=./ssl.test.com.crt --dry-run=client -n cert-manager  -o yaml  | kubectl apply -f -   
```

- 编写对应的`Bundle`
  - `Bundle`会自己在对应的`namespcae`下面生成一个 `public-bundle` 的 `configMap`

```yaml
apiVersion: trust.cert-manager.io/v1alpha1
kind: Bundle
metadata:
  name: public-bundle
spec:
  sources:
# 为true会帮你更新对应容器中的CA证书
# https://cert-manager.io/docs/trust/trust-manager/#securely-maintaining-a-trust-manager-installation
  - useDefaultCAs: true
  - secret:
      name: "ssl-test-web-crt"
      key: "ssl.test.com.crt"
  target:
    configMap:
      key: "ca-certificates.crt"
# 这里要给对应要生成 public-bundle configMap 的namespace 打上label
# kubectl label ns default trust=enabled
    namespaceSelector:
      matchLabels:
        trust: enabled
```

- 我们只需要在对应的容器中挂载`public-bundle`到`/etc/ssl/certs`

```yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization

resources:
- ../../base

.....

patchesJson6902:
  - target:
      group: apps
      version: v1
      kind: Deployment
      name: https-client
    patch: |-
.....
      - op: add
        path: /spec/template/spec/volumes/-
        value:
        #声明Volumes
          name: public-bundle
          configMap:
            name: public-bundle
            defaultMode: 0644
            optional: false
      - op: add
        path: /spec/template/spec/containers/0/volumeMounts/-
        value:
        # VolumeMounts 挂载到/etc/ssl/certs/
          name: public-bundle
          mountPath: /etc/ssl/certs/
          readOnly: true

images:
  - name: https-client
    newTag: v1.0
    newName: https-client
```

- 如果成功了，在容器中就会如下显示

```shell
root@https-client-6b74cfbddd-2nw2r:/app# ls -ltr /etc/ssl/certs/ca-certificates.crt              
lrwxrwxrwx. 1 root root 26 Mar 23 06:26 /etc/ssl/certs/ca-certificates.crt -> ..data/ca-certificates.crt
```

# 最后

对比上述的两种方法

第一种方法我觉得比较简单，无须额外安装其它组件，但是对配置的入侵性比较高，需要额外配置一个有`ca-certificates`的`init-container`

第二种方法需要额外安装多一个`trust-manager`，但是落实到具体的`Pod`配置只需要额外挂载一个 `ConfigMap` 到对应的证书路径即可，同时对应的`bundle/public-bundle`也是支持自动更新的，即你的`secret`更新了，它也会自动同步。

```shell
[root@rocky-testing ~]# kubectl get events
LAST SEEN   TYPE     REASON              OBJECT                               MESSAGE
52m         Normal   Synced              bundle/public-bundle                 Successfully synced Bundle to namespaces that match this label selector: trust=enabled
```

当然除了上述两种方法，还有没有**更简单的方法，甚至不需要额外的配置**🤔

有的！如果你公司有一份支持多域名的证书，就可以直接使用该 crt(一般叫做xxx_chain.crt或者.pem) 和 key

如何查看？使用 `openssl x509 -noout -text -in server.crt` 查看证书信息 类似如下

```text
            ·······
 # 如果 SAN (Subject Alternative Name) 中包含多个域名 或者 通配符 *.test.com 
            X509v3 Subject Alternative Name: 
                DNS:*.test.com, DNS:test.com, DNS:abc.test.com
    Signature Algorithm: sha256WithRSAEncryption
    Signature Value:
    ······
```

这样你就可以用 `abc.test.com` 等类似的`xxx.test.com` 的二级域名 (注意三级不行 如 `xxx.adb.test.com`)

再通过修改hosts或者k8s中的coredns，就可以使用不需要额外配置使用`Https`了 😊