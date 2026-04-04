1, Code review 理解mpsc
2，Buffer 的对象池, 不能使用Fixed Buffer 也不能使用 mmap 一次创建一个足够大的 buffer，就用 mi_malloc 创建
3, Session 需要一个 UserData 或者是map<string, void*>
3，加入 server 事件 on_inited() on_stopped() on_connected() on_disconnected() on_data()
4，conn/connector