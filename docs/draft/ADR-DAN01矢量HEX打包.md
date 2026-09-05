# ADR: DAN01矢量HEX打包

## 摘要
四元数/加速度/角速度/磁场各用一个HEX属性，不上欧拉角。

## 决定
1. `quat`、`accel`、`gyro`、`mag` 各一个只读 HEX 字符串。
2. 不上 `roll`/`pitch`/`yaw`。
3. HEX：小写、无 `0x`、与 AS201 数据段相同的小端 int16 原字节。
