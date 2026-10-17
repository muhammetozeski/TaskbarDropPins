# Taskbar Drop Pins

Windows 11 görev çubuğuna Dosya Gezgini veya masaüstünden dosya, klasör ve `.lnk` kısayollarını sürükleyerek sabitleyen Windhawk modu. Öğeler mevcut uygulama simgelerinin arasında yer alır; Windows'un kendi görev çubuğu ve normal sürükleyerek sıralama davranışı kullanılır.

## Çalışma biçimi

`explorer.exe` içindeki `DoDragDrop` çağrısı izlenir. Modun `IDropSource` sarmalayıcısı, sürükleme görev çubuğunun üzerindeyken bağlantı geri bildirimi verir. Fare bırakıldığında kaynak dosya aktarımı iptal edilir; kaynak dosyayı taşımadan ayrı bir iş parçacığında Shell PinManager ile sabitleme yapılır. Görev çubuğu düğmelerinin ekran konumları yalnızca okunur ve bırakılan yere göre `MoveTaskbarPin` çağrılır.

Dosya veya klasör için normal bir uygulama kısayolu üretilir: hedef 13.312 baytlık `TaskbarDropPins.Launcher-v1.exe`, bağımsız değişken ise açılacak öğenin tam yoludur. Başlatıcı `ShellExecuteExW` kullanır. `.lnk` dosyalarının kalıcı bir kopyası açılır; Windows kısayolun hedefini, bağımsız değişkenlerini ve çalışma klasörünü kendisi işler. Aynı programa giden farklı `.lnk` dosyaları ayrı uygulama kimlikleri alır.

Kimlik, küçük harfe çevrilmiş ve sözdizimsel olarak normalleştirilmiş yolun UTF-16 kod birimleri üzerinde 64 bit Fowler–Noll–Vo benzeri döngüyle üretilen 16 onaltılık karakterdir. Görünen ada eklenmez: `links\<kimlik>\Karışık.lnk` ve `AppUserModel.ID=TaskbarDropPins.<kimlik>` kullanılır. İlk geliştirme sürümünde bu kimlik dosya adının sonunda bulunuyordu.

Simgeler Windows'tan alınır. Windows Imaging Component ile kayıpsız, saydamlığı koruyan 32 bit PNG kareli ICO dosyasına yazılır. İlk sürümün `IPicture::SaveAsFile` yöntemi 4 bit palete düşürdüğü için değiştirilmiştir.

`Shift` basılıyken sürükleme asıl Windows davranışına bırakılır. Mevcut görev çubuğu simgeleri Windows'un normal işleyicisiyle sıralanır. Modun eski OLE hedef işleyicilerine yönelik dönüşümü de bulunur; bu bilgisayarda çalışan yol kaynak tarafındaki `DoDragDrop` yakalayıcısıdır.

## Kalıcı dosyalar

Proje: `C:\E\KodlamaProjeleri\c++\TaskbarDropPins`.

Bu bilgisayardaki kalıcı mod verileri: `C:\E\kp\scoop\persist\windhawk\AppData\Engine\ModsWritable\mod-storage\taskbar-drop-pins`.

- `links\<kimlik>\<görünen ad>.lnk`: üretilen kısayollar.
- `targets\<kimlik>.lnk`: bırakılan özgün `.lnk` dosyalarının kopyaları.
- `icons\<kimlik>-v2.ico`: tam renkli simgeler.
- `TaskbarDropPins.Launcher-v1.exe`: ortak yerel başlatıcı.

Veriler sabitlenen öğeler tarafından kullanıldığı için mod kapatılınca silinmez. Kaynak dosya ve klasörler yerlerinde kalır.

## Kaynaklar ve betikler

- `src/taskbar-drop-pins.wh.cpp`: mod, kısayol üretimi ve simge kaydı.
- `src/source-drop.h`: Dosya Gezgini sürükleme kaynağı sarmalayıcısı.
- `src/modern-drop.h`: Shell PinManager ve görev çubuğu konum okuma kodu.
- `src/launcher.cpp`: küçük yerel dosya açıcı.
- `Build.ps1`: kurulu Windhawk derleyicisiyle mod ve tek dosyalık dağıtım kaynağı üretimi.
- `Install.ps1`: derlenmiş modun kurulu taşınabilir Windhawk'a eklenmesi.
- `Test.ps1`: ayrı, boş bir NewTemp klasöründe Shell sözleşmesi ve simge kaydı testleri.
- `dist/`: derlenmiş dağıtım dosyaları; Git'e eklenmez.

## Derleme ve kurulum

PowerShell 7 ile proje klasöründe `pwsh -File .\Build.ps1` ve ardından `pwsh -File .\Install.ps1` komutlarını kullanın. Betikler varsayılan olarak `C:\E\kp\scoop\apps\windhawk\current` kurulumunu kullanır; `-WindhawkPath` ile başka kurulum verilebilir. İnternetten bağımlılık indirilmez.

`pwsh -File .\Install.ps1 -Disable` yeni sürükleme dönüşümünü kapatır. Sabitlenen öğeler kalıcı başlatıcıyla açılmaya devam eder. Öğeler Windows'un görev çubuğu menüsünden kaldırılır.

## Doğrulama — 17 Ekim 2026

Windhawk 1.7.3 ve Windows 11 26H2, derleme 26300.9711 üzerinde klasör, dosya ve `.lnk` ekleme, açılma ve simgelerin arasına sürükleyerek sıralama kullanıcı tarafından doğrulandı.

Otomatik testler: gerçek Shell veri nesneleri; Türkçe ve boşluk içeren yollar; farklı öğelerin ayrı kimlikleri; tekrar işlemde aynı kimliğin kullanılması; mevcut mod kısayollarının yeniden dönüştürülmemesi; `.lnk` bağımsız değişkenleri, çalışma klasörü ve kaydedilmiş pencere gösterimi; aynı EXE'ye farklı kısayollar; karışık çoklu seçim; kaydedilen simgenin her renk ve saydamlık baytının özgün Shell simgesiyle aynı olması. Bu testler geçti.

Test girdileri ve çıktıları NewTemp'in oluşturduğu ayrı klasörlerde tutulur. Proje veya kalıcı çalışma verileri geçici klasöre konulmaz.

## Teknik dayanaklar

- [Microsoft: SHDoDragDrop](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shdodragdrop)
- [Microsoft: IDropTarget::DragEnter](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nf-oleidl-idroptarget-dragenter)
- [Windhawk: mod oluşturma](https://github.com/ramensoftware/windhawk/wiki/Creating-a-new-mod)

PinManager, Windows Shell'in özel COM arayüzüdür; bu sürümde kullanılan arayüz kimlikleri ve yöntem sırası `src/modern-drop.h` dosyasında izole edilmiştir.