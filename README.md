# Taskbar Drop Pins

Windows 11 görev çubuğuna dosya, klasör ve `.lnk` kısayollarını sürükleyerek sabitlemek için Windhawk modu.

## Tasarım

Mod, görev çubuğunun mevcut `IDropTarget` girişini yakalar. Windows'un doğrudan sabitleyemediği öğeler için kalıcı bir başlatıcı kısayolu oluşturur ve bu kısayolun Shell veri nesnesini Windows'un asıl bırakma işleyicisine verir. Sabitleme, yer seçimi, simgeleri sürükleyerek sıralama ve sabitlemeyi kaldırma işlemlerini Windows yapar.

Başlatıcı, kaynak dosyayı veya klasörü taşımadan Windows Shell üzerinden açar. `.lnk` dosyalarının bir kopyası kalıcı mod verilerinde tutulur; hedef, bağımsız değişkenler, çalışma klasörü ve diğer kısayol özellikleri Windows tarafından işlenir. Çalıştırılabilir uygulama kısayolları doğrudan Windows'a aktarılır.

`Shift` basılıyken sürükleme asıl Windows davranışına bırakılır. Böylece bir dosyayı açık bir uygulama düğmesinin üzerinde bekleterek o uygulamaya götürmek mümkündür.

## Dosyalar

- `src/taskbar-drop-pins.wh.cpp`: mod kaynak kodu.
- `src/launcher.cpp`: küçük yerel dosya açıcı.
- `Build.ps1`: Windhawk'ın kurulu derleyicisiyle derleme ve tek dosyalık mod üretimi.
- `Install.ps1`: derlenmiş modun kurulu taşınabilir Windhawk'a eklenmesi.
- `dist/`: derlenmiş dağıtım dosyaları ve Windhawk'a aktarılabilen tek kaynak dosyası.

## Durum

Geliştirme sürümüdür. Derleme ve gerçek sürükleyip bırakma doğrulamasının sonuçları burada kaydedilir.
