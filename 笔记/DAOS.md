SVN网址：svn://10.255.190.66/Storage，用户名/密码：admin/admin

虚拟机密码：000000

----------------------------------

DAOS依赖的第三方库概述：

Protocol Buffer：
1、Google提供的一种数据序列化协议；
2、轻便高效的结构化数据存储格式，可以用于结构化数据序列化，很适合做数据存储或 RPC 数据交换格式；
3、可用于通讯协议、数据存储等领域的语言无关、平台无关、可扩展的序列化结构数据格式；

HWLOC：
1、旨在简化发现机器的硬件资源；
2、适用于任何寻求在计算中利用代码/数据局部性的项目；
3、是否可以理解为CPU的亲和性/绑核, 以达到充分利用CPU的内部缓存的目的； 


ARGOBOTS:
1、轻量级的线程、协程和任务框架；

-----------------------------------

配置文件:
/etc/daos/daos_agent.yml 
/etc/daos/daos_control.yml

/etc/daos/daos_server.yml
  env_vars:															// 新增如下配置, 打印debug日志

  - D_LOG_MASK=DEBUG/ERROR/INFO
  - DD_SUBSYS=all/pool/vos/eio/mgmt/misc/mem
  - DD_MASK=all/trace/test/mgmt

-----------------------------------

```
// storage
[root@daos129 tmp]# dmg -i storage format			// SCM/NVME初始化

// system
[root@daos129 tmp]# dmg -i system query				// 查看服务情况
[root@daos129 tmp]# dmg -i system leader-query		// 当前server的leader
[root@daos129 tmp]# dmg -i system list-pools		// 列出所有的池

// pool
[root@daos129 tmp]# dmg pool create --size=NGB		// 创池
```

