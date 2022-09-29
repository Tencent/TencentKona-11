# Tencent JDK 11
![Tencent-kona.jpg](/uploads/53E12E7E40644E75B522447E91862F81/Tencent-kona.jpg)
 - 免费、定制优化的OpenJDK版本
 - 基于OpenJDK 11，兼容Java标准接口
 - 经过数据平台部多个系统内测使用，稳定性有高度保障
 - 经SPECjvm、SPECjbb测试，性能比肩Oracle、Corretto、Azul、Dragonwell等主流JDK11版本
 - 支持大堆GC优化、内存资源成本优化等新特性，针对大数据、机器学习、云场景、微服务等场景持续优化
 - 季度同步，保障安全更新

# 用户指南
- [用户指南](https://git.code.oa.com/JDK/tencentJDK11/wikis/User_Guide)
- 针对JDK 11版本升级中的兼容性问题，Oteam准备了[Kona JDK 11 迁移指南](https://iwiki.oa.tencent.com/pages/viewpage.action?pageId=79253608)
- 常见问题（完善中)

# 开发指南
- [开发指南](https://git.code.oa.com/JDK/tencentJDK/wikis/How_to_Contribute)（与TencentKona 8保持一致）

# Release Notes

## [下载地址](http://jdk.oa.com/download.html)

## 2020.04 [TencentKona11.0.7-ea](http://bia.oa.com/mirror/dev/11.0.7/linux-x86_64/ea/b1/TencentKona11.0.7.b1-ea_jdk_linux-x86_64.tar.gz)
FEAT(g1): Backport of JEP 346: Promptly Return Unused Committed Memory from G1
FEAT(g1): Implementation of JDK-8204089 Promptly Return Unused Committed Memory from G1
FEAT(svc): Enable PrintExtendThreadInfo by default
FEAT(GC): Backport of JEP 344: Abortable Mixed Collections for G1 GC
