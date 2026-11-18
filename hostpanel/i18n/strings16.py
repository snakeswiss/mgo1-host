# The game settings this PC keeps (the last ones saved to a bot, a new bot gets them; the presets) and the
# maps only the PC players have (rules.h); columns: key, en, fr, de, it, es, pt-BR, ru, ja, ko, zh-Hans, zh-Hant
S16 = [
("p_presets", "GAME SETTINGS KEPT ON THIS PC", "RÉGLAGES DE PARTIE MÉMORISÉS SUR CE PC", "AUF DIESEM PC GESPEICHERTE SPIELEINSTELLUNGEN",
 "IMPOSTAZIONI PARTITA SALVATE SU QUESTO PC", "AJUSTES DE PARTIDA GUARDADOS EN ESTE PC", "CONFIG. DE PARTIDA SALVAS NESTE PC",
 "НАСТРОЙКИ ИГРЫ, СОХРАНЁННЫЕ НА ЭТОМ ПК", "この PC に保存したゲーム設定", "이 PC에 저장된 게임 설정", "本机保存的游戏设置", "本機儲存的遊戲設定"),
("i_last_game", "LAST SAVED", "DERNIERS ENREGISTRÉS", "ZULETZT GESPEICHERT", "ULTIME SALVATE", "ÚLTIMOS GUARDADOS", "ÚLTIMAS SALVAS",
 "ПОСЛЕДНИЕ СОХРАНЁННЫЕ", "最後に保存", "마지막 저장", "上次保存", "上次儲存"),
("v_last_game", "LAST SAVED: %s", "DERNIERS ENREGISTRÉS : %s", "ZULETZT GESPEICHERT: %s", "ULTIME SALVATE: %s", "ÚLTIMOS GUARDADOS: %s",
 "ÚLTIMAS SALVAS: %s", "ПОСЛЕДНИЕ СОХРАНЁННЫЕ: %s", "最後に保存：%s", "마지막 저장: %s", "上次保存：%s", "上次儲存：%s"),
("h_use_last", "USE: puts the last game settings saved from this PC (%s) in the form above, the password lock too; SAVE then sends them to this bot. "
 "A new bot (BOTS) gets them at once.",
 "UTILISER : met les derniers réglages de partie enregistrés depuis ce PC (%s) dans le formulaire ci-dessus, mot de passe compris ; ENREGISTRER "
 "les envoie ensuite à ce bot. Un nouveau bot (BOTS) les reçoit d'office.",
 "VERWENDEN: setzt die zuletzt von diesem PC gespeicherten Spieleinstellungen (%s) ins Formular oben, samt Passwortsperre; SPEICHERN schickt sie "
 "dann an diesen Bot. Ein neuer Bot (BOTS) bekommt sie sofort.",
 "USA: mette nel modulo qui sopra le ultime impostazioni partita salvate da questo PC (%s), password compresa; SALVA le invia poi a questo bot. "
 "Un nuovo bot (BOT) le riceve subito.",
 "USAR: pone en el formulario de arriba los últimos ajustes de partida guardados desde este PC (%s), contraseña incluida; GUARDAR los envía "
 "luego a este bot. Un bot nuevo (BOTS) los recibe al instante.",
 "USAR: coloca no formulário acima as últimas configurações de partida salvas deste PC (%s), com a senha; SALVAR as envia depois a este bot. "
 "Um bot novo (BOTS) as recebe na hora.",
 "ВЫБРАТЬ: подставляет в форму выше последние настройки игры, сохранённые с этого ПК (%s), вместе с паролем; СОХРАНИТЬ отправит их этому "
 "боту. Новый бот (БОТЫ) получает их сразу.",
 "使う：この PC から最後に保存したゲーム設定（%s）をパスワードロックごと上のフォームに入れます。「保存」でこのボットに送ります。"
 "新しいボット（ボット）には自動で送られます。",
 "사용: 이 PC에서 마지막으로 저장한 게임 설정(%s)을 비밀번호 잠금까지 위 양식에 넣습니다. 저장하면 이 봇에 보냅니다. "
 "새 봇(봇 페이지)은 바로 이 설정을 받습니다.",
 "使用：将本机上次保存的游戏设置（%s）连同密码锁填入上方表单，点“保存”发送给此机器人。新机器人（机器人页面）会立即获得这些设置。",
 "使用：將本機上次儲存的遊戲設定（%s）連同密碼鎖填入上方表單，按「儲存」傳送給此機器人。新機器人（機器人頁面）會立即取得這些設定。"),
("m_last_filled", "The last settings are in the form: SAVE sends them to bot %d.",
 "Derniers réglages dans le formulaire : ENREGISTRER les envoie au bot %d.",
 "Letzte Einstellungen im Formular: SPEICHERN schickt sie an Bot %d.", "Ultime impostazioni nel modulo: SALVA le invia al bot %d.",
 "Últimos ajustes en el formulario: GUARDAR los envía al bot %d.", "Últimas configurações no formulário: SALVAR as envia ao bot %d.",
 "Последние настройки в форме: СОХРАНИТЬ отправит их боту %d.", "最後の設定をフォームに入れました。「保存」でボット %d に送ります。",
 "마지막 설정을 양식에 넣었습니다. 저장하면 봇 %d에 보냅니다.", "上次的设置已填入表单：点“保存”发送给机器人 %d。",
 "上次的設定已填入表單：按「儲存」傳送給機器人 %d。"),
("h_use_preset", "USE: puts the preset %s in the form above (nothing is sent before SAVE). FORGET: this PC no longer keeps it.",
 "UTILISER : met le préréglage %s dans le formulaire ci-dessus (rien n'est envoyé avant ENREGISTRER). OUBLIER : ce PC ne le garde plus.",
 "VERWENDEN: setzt die Vorlage %s ins Formular oben (vor SPEICHERN wird nichts gesendet). VERGESSEN: dieser PC behält sie nicht mehr.",
 "USA: mette il preset %s nel modulo qui sopra (nulla viene inviato prima di SALVA). DIMENTICA: questo PC non lo conserva più.",
 "USAR: pone el preajuste %s en el formulario de arriba (nada se envía antes de GUARDAR). OLVIDAR: este PC deja de guardarlo.",
 "USAR: coloca a predefinição %s no formulário acima (nada é enviado antes de SALVAR). ESQUECER: este PC deixa de guardá-la.",
 "ВЫБРАТЬ: подставляет шаблон %s в форму выше (до СОХРАНИТЬ ничего не отправляется). ЗАБЫТЬ: этот ПК больше его не хранит.",
 "使う：プリセット %s を上のフォームに入れます（「保存」まで何も送りません）。削除：この PC から消します。",
 "사용: 프리셋 %s을(를) 위 양식에 넣습니다(저장하기 전에는 아무것도 보내지 않음). 삭제: 이 PC에서 지웁니다.",
 "使用：将预设 %s 填入上方表单（点“保存”之前不会发送）。删除：本机不再保存它。",
 "使用：將預設 %s 填入上方表單（按「儲存」之前不會傳送）。刪除：本機不再儲存它。"),
("m_preset_filled", "%s is in the form: SAVE sends it to bot %d.", "%s dans le formulaire : ENREGISTRER l'envoie au bot %d.",
 "%s steht im Formular: SPEICHERN schickt sie an Bot %d.", "%s è nel modulo: SALVA lo invia al bot %d.",
 "%s está en el formulario: GUARDAR lo envía al bot %d.", "%s está no formulário: SALVAR a envia ao bot %d.",
 "%s в форме: СОХРАНИТЬ отправит его боту %d.", "%s をフォームへ。「保存」でボット %d に送信。",
 "%s을(를) 양식에 넣었습니다. 저장하면 봇 %d에 보냅니다.", "已将 %s 填入表单：点“保存”发送给机器人 %d。", "已將 %s 填入表單：按「儲存」傳送給機器人 %d。"),
("c_forget_preset", "Forget the preset %s on this PC? The bots keep their settings.", "Oublier le préréglage %s sur ce PC ? Les bots gardent leurs réglages.",
 "Die Vorlage %s auf diesem PC vergessen? Die Bots behalten ihre Einstellungen.",
 "Dimenticare il preset %s su questo PC? I bot mantengono le loro impostazioni.", "¿Olvidar el preajuste %s en este PC? Los bots conservan sus ajustes.",
 "Esquecer a predefinição %s neste PC? Os bots mantêm suas configurações.", "Забыть шаблон %s на этом ПК? Боты сохранят свои настройки.",
 "この PC からプリセット %s を削除しますか？ボットの設定はそのままです。", "이 PC에서 프리셋 %s을(를) 삭제할까요? 봇의 설정은 그대로입니다.",
 "在本机删除预设 %s？机器人的设置保持不变。", "在本機刪除預設 %s？機器人的設定保持不變。"),
("g_preset_name", "PRESET NAME", "NOM DU PRÉRÉGLAGE", "NAME DER VORLAGE", "NOME DEL PRESET", "NOMBRE DEL PREAJUSTE", "NOME DA PREDEFINIÇÃO",
 "ИМЯ ШАБЛОНА", "プリセット名", "프리셋 이름", "预设名称", "預設名稱"),
("h_preset_name", "The name to keep the form under (up to 24 plain characters). The name of a kept preset replaces it.",
 "Le nom sous lequel garder le formulaire (jusqu'à 24 caractères simples). Le nom d'un préréglage mémorisé le remplace.",
 "Der Name, unter dem das Formular gespeichert wird (bis 24 einfache Zeichen). Der Name einer gespeicherten Vorlage ersetzt diese.",
 "Il nome con cui salvare il modulo (fino a 24 caratteri semplici). Il nome di un preset salvato lo sostituisce.",
 "El nombre con el que guardar el formulario (hasta 24 caracteres simples). El nombre de un preajuste guardado lo reemplaza.",
 "O nome com que guardar o formulário (até 24 caracteres simples). O nome de uma predefinição salva a substitui.",
 "Имя, под которым сохранить форму (до 24 простых символов). Имя уже сохранённого шаблона заменяет его.",
 "フォームを保存する名前（半角24文字まで）。保存済みのプリセット名なら上書きします。",
 "양식을 보관할 이름(영문 24자까지). 이미 저장된 프리셋 이름이면 그 프리셋을 바꿉니다.",
 "保存此表单所用的名称（最多 24 个英文字符）。若与已保存的预设同名则替换它。",
 "儲存此表單所用的名稱（最多 24 個英文字元）。若與已儲存的預設同名則取代它。"),
("b_preset_save", "SAVE AS PRESET", "ENREGISTRER COMME PRÉRÉGLAGE", "ALS VORLAGE SPEICHERN", "SALVA COME PRESET", "GUARDAR COMO PREAJUSTE",
 "SALVAR COMO PREDEFINIÇÃO", "СОХРАНИТЬ КАК ШАБЛОН", "プリセットとして保存", "프리셋으로 저장", "存为预设", "存為預設"),
("h_preset_save", "Keeps the form on this PC under the name above, its password lock encrypted for your Windows account (up to %d presets).",
 "Garde le formulaire sur ce PC sous le nom ci-dessus, son mot de passe chiffré pour votre compte Windows (jusqu'à %d préréglages).",
 "Speichert das Formular unter dem Namen oben auf diesem PC, die Passwortsperre für dein Windows-Konto verschlüsselt (bis %d Vorlagen).",
 "Salva il modulo su questo PC con il nome qui sopra, la password cifrata per il tuo account Windows (fino a %d preset).",
 "Guarda el formulario en este PC con el nombre de arriba, su contraseña cifrada para tu cuenta de Windows (hasta %d preajustes).",
 "Guarda o formulário neste PC com o nome acima, a senha criptografada para sua conta do Windows (até %d predefinições).",
 "Сохраняет форму на этом ПК под именем выше, пароль игры зашифрован для вашей учётной записи Windows (до %d шаблонов).",
 "フォームを上の名前でこの PC に保存します（パスワードロックは Windows アカウント用に暗号化、プリセットは %d 個まで）。",
 "양식을 위 이름으로 이 PC에 보관합니다(비밀번호 잠금은 Windows 계정용으로 암호화, 프리셋은 %d개까지).",
 "以上方名称将表单保存在本机（密码锁按你的 Windows 账户加密，最多 %d 个预设）。",
 "以上方名稱將表單儲存在本機（密碼鎖依你的 Windows 帳戶加密，最多 %d 個預設）。"),
("m_preset_saved", "Preset %s kept on this PC.", "Préréglage %s mémorisé sur ce PC.", "Vorlage %s auf diesem PC gespeichert.",
 "Preset %s salvato su questo PC.", "Preajuste %s guardado en este PC.", "Predefinição %s salva neste PC.", "Шаблон %s сохранён на этом ПК.",
 "プリセット %s をこの PC に保存しました。", "프리셋 %s을(를) 이 PC에 저장했습니다.", "已在本机保存预设 %s。", "已在本機儲存預設 %s。"),
("c_preset_replace", "Replace the preset %s with the form?", "Remplacer le préréglage %s par le formulaire ?", "Die Vorlage %s durch das Formular ersetzen?",
 "Sostituire il preset %s con il modulo?", "¿Reemplazar el preajuste %s por el formulario?", "Substituir a predefinição %s pelo formulário?",
 "Заменить шаблон %s содержимым формы?", "プリセット %s をこのフォームの内容で上書きしますか？", "프리셋 %s을(를) 이 양식으로 바꿀까요?",
 "用此表单替换预设 %s？", "用此表單取代預設 %s？"),
("e_preset_name", "A preset's name takes plain characters only (English letters, digits, signs).",
 "Le nom d'un préréglage n'accepte que des caractères simples (lettres sans accent, chiffres, signes).",
 "Der Name einer Vorlage nimmt nur einfache Zeichen (Buchstaben ohne Umlaute, Ziffern, Zeichen).",
 "Il nome di un preset accetta solo caratteri semplici (lettere senza accenti, cifre, segni).",
 "El nombre de un preajuste solo acepta caracteres simples (letras sin acentos, dígitos, signos).",
 "O nome de uma predefinição só aceita caracteres simples (letras sem acento, dígitos, sinais).",
 "Имя шаблона: только латинские буквы, цифры и знаки.", "プリセット名には半角英数字と記号のみ使えます。",
 "프리셋 이름에는 영문, 숫자, 기호만 쓸 수 있습니다.", "预设名称只能使用英文字母、数字和符号。", "預設名稱只能使用英文字母、數字和符號。"),
("e_presets_full", "%d presets at most: forget one, or give a kept one's name to replace it.",
 "%d préréglages au plus : oubliez-en un, ou donnez le nom d'un préréglage mémorisé pour le remplacer.",
 "Höchstens %d Vorlagen: vergiss eine oder gib den Namen einer gespeicherten an, um sie zu ersetzen.",
 "Al massimo %d preset: dimenticane uno, o indica il nome di uno salvato per sostituirlo.",
 "%d preajustes como máximo: olvida uno, o escribe el nombre de uno guardado para reemplazarlo.",
 "No máximo %d predefinições: esqueça uma, ou use o nome de uma salva para substituí-la.",
 "Не больше %d шаблонов: забудьте один или укажите имя сохранённого, чтобы заменить его.",
 "プリセットは %d 個までです。どれかを削除するか、保存済みの名前を入れて上書きしてください。",
 "프리셋은 %d개까지입니다. 하나를 삭제하거나 저장된 이름을 입력해 바꾸세요.",
 "最多 %d 个预设：请删除一个，或输入已保存预设的名称来替换它。", "最多 %d 個預設：請刪除一個，或輸入已儲存預設的名稱來取代它。"),
("presets_hint", "Each SAVE keeps the game settings on this PC as the last ones saved (the password lock encrypted for your Windows account): a new "
 "bot (BOTS) gets them at once, USE puts them back in the form, on a reinstalled server for one. A preset keeps the form under a name, for any bot "
 "of any server.",
 "Chaque ENREGISTRER garde sur ce PC les réglages de partie comme les derniers enregistrés (le mot de passe chiffré pour votre compte Windows) : "
 "un nouveau bot (BOTS) les reçoit d'office, UTILISER les remet dans le formulaire, par exemple sur un serveur réinstallé. Un préréglage garde "
 "le formulaire sous un nom, pour n'importe quel bot de n'importe quel serveur.",
 "Jedes SPEICHERN behält die Spieleinstellungen als zuletzt gespeicherte auf diesem PC (die Passwortsperre für dein Windows-Konto verschlüsselt): "
 "ein neuer Bot (BOTS) bekommt sie sofort, VERWENDEN setzt sie wieder ins Formular, etwa auf einem neu installierten Server. Eine Vorlage behält "
 "das Formular unter einem Namen, für jeden Bot jedes Servers.",
 "Ogni SALVA conserva su questo PC le impostazioni partita come ultime salvate (la password cifrata per il tuo account Windows): un nuovo bot "
 "(BOT) le riceve subito, USA le rimette nel modulo, per esempio su un server reinstallato. Un preset conserva il modulo con un nome, per "
 "qualsiasi bot di qualsiasi server.",
 "Cada GUARDAR conserva en este PC los ajustes de partida como los últimos guardados (la contraseña cifrada para tu cuenta de Windows): un bot "
 "nuevo (BOTS) los recibe al instante, USAR los vuelve a poner en el formulario, por ejemplo en un servidor reinstalado. Un preajuste guarda el "
 "formulario con un nombre, para cualquier bot de cualquier servidor.",
 "Cada SALVAR guarda neste PC as configurações da partida como as últimas salvas (a senha criptografada para sua conta do Windows): um bot novo "
 "(BOTS) as recebe na hora, USAR as coloca de volta no formulário, por exemplo num servidor reinstalado. Uma predefinição guarda o formulário com "
 "um nome, para qualquer bot de qualquer servidor.",
 "Каждое СОХРАНИТЬ запоминает на этом ПК настройки игры как последние сохранённые (пароль игры зашифрован для вашей учётной записи Windows): "
 "новый бот (БОТЫ) получает их сразу, ВЫБРАТЬ возвращает их в форму, например на переустановленном сервере. Шаблон хранит форму под именем, "
 "для любого бота любого сервера.",
 "「保存」するたびに、ゲーム設定を最後に保存した設定としてこの PC に残します（パスワードロックは Windows アカウント用に暗号化）。"
 "新しいボット（ボット）には自動で送られ、「使う」でフォームに戻せます（再インストールしたサーバーなど）。"
 "プリセットはフォームを名前付きで保存し、どのサーバーのどのボットにも使えます。",
 "저장할 때마다 게임 설정을 마지막 저장 설정으로 이 PC에 보관합니다(비밀번호 잠금은 Windows 계정용으로 암호화). 새 봇(봇 페이지)은 "
 "바로 이 설정을 받고, 사용을 누르면 양식에 다시 넣습니다(다시 설치한 서버 등). 프리셋은 양식을 이름으로 보관해 어느 서버의 어느 봇에도 "
 "쓸 수 있습니다.",
 "每次“保存”都会把游戏设置作为上次保存的设置留在本机（密码锁按你的 Windows 账户加密）：新机器人（机器人页面）会立即获得这些设置，"
 "“使用”可将其重新填入表单，例如用于重装后的服务器。预设以名称保存表单，可用于任何服务器的任何机器人。",
 "每次「儲存」都會把遊戲設定作為上次儲存的設定留在本機（密碼鎖依你的 Windows 帳戶加密）：新機器人（機器人頁面）會立即取得這些設定，"
 "「使用」可將其重新填入表單，例如用於重新安裝後的伺服器。預設以名稱儲存表單，可用於任何伺服器的任何機器人。"),
# the BOTS page: a new bot gets the last game settings
("h_addbot_last", "Creates the bot on the server, opens its UDP port there, sends it the last game settings saved from this PC and the account, and "
 "starts it: its game appears in the lobby about a minute later.",
 "Crée le bot sur le serveur, y ouvre son port UDP, lui envoie les derniers réglages de partie enregistrés depuis ce PC et le compte, et le "
 "démarre : sa partie apparaît dans le lobby environ une minute plus tard.",
 "Legt den Bot auf dem Server an, öffnet dort seinen UDP-Port, schickt ihm die zuletzt von diesem PC gespeicherten Spieleinstellungen und das "
 "Konto und startet ihn: sein Spiel erscheint etwa eine Minute später in der Lobby.",
 "Crea il bot sul server, vi apre la sua porta UDP, gli invia le ultime impostazioni partita salvate da questo PC e l'account, e lo avvia: la sua "
 "partita appare nella lobby circa un minuto dopo.",
 "Crea el bot en el servidor, abre allí su puerto UDP, le envía los últimos ajustes de partida guardados desde este PC y la cuenta, y lo inicia: "
 "su partida aparece en el lobby un minuto después.",
 "Cria o bot no servidor, abre lá a porta UDP, envia as últimas configurações de partida salvas deste PC e a conta, e o inicia: a partida aparece "
 "no lobby cerca de um minuto depois.",
 "Создаёт бота на сервере, открывает там его порт UDP, отправляет ему последние настройки игры, сохранённые с этого ПК, и аккаунт и запускает: "
 "его игра появится в лобби примерно через минуту.",
 "サーバーにボットを作成し、UDP ポートを開け、この PC から最後に保存したゲーム設定とアカウントを送って起動します。約1分後にゲームがロビーに現れます。",
 "서버에 봇을 만들고 UDP 포트를 연 뒤, 이 PC에서 마지막으로 저장한 게임 설정과 계정을 보내고 시작합니다. 약 1분 뒤 게임이 로비에 나타납니다.",
 "在服务器上创建机器人，打开其 UDP 端口，发送本机上次保存的游戏设置和账号并启动：约一分钟后其游戏出现在大厅。",
 "在伺服器上建立機器人，開啟其 UDP 連接埠，傳送本機上次儲存的遊戲設定和帳號並啟動：約一分鐘後其遊戲出現在大廳。"),
("m_bot_added_last", "Bot %d added (UDP %d) with the last game settings.", "Bot %d ajouté (UDP %d) avec les derniers réglages de partie.",
 "Bot %d hinzugefügt (UDP %d), mit den letzten Spieleinstellungen.", "Bot %d aggiunto (UDP %d) con le ultime impostazioni partita.",
 "Bot %d añadido (UDP %d) con los últimos ajustes de partida.", "Bot %d adicionado (UDP %d) com as últimas configurações de partida.",
 "Бот %d добавлен (UDP %d) с последними настройками игры.", "ボット %d を追加しました（UDP %d）。最後のゲーム設定を使います。",
 "봇 %d 추가됨(UDP %d), 마지막 게임 설정 적용.", "已添加机器人 %d（UDP %d），使用上次的游戏设置。", "已新增機器人 %d（UDP %d），使用上次的遊戲設定。"),
("m_bot_added_nolast", "Bot %d added, without the last game settings: %s", "Bot %d ajouté, sans les derniers réglages de partie : %s",
 "Bot %d hinzugefügt, ohne die letzten Spieleinstellungen: %s", "Bot %d aggiunto, senza le ultime impostazioni partita: %s",
 "Bot %d añadido, sin los últimos ajustes de partida: %s", "Bot %d adicionado, sem as últimas configurações de partida: %s",
 "Бот %d добавлен без последних настроек игры: %s", "ボット %d を追加しましたが、最後のゲーム設定は送れませんでした：%s",
 "봇 %d 추가됨, 마지막 게임 설정은 보내지 못함: %s", "已添加机器人 %d，但未能发送上次的游戏设置：%s", "已新增機器人 %d，但未能傳送上次的遊戲設定：%s"),
# the rotation: a map only the PC players have
("v_pc_map", "PC", "PC", "PC", "PC", "PC", "PC", "ПК", "PC", "PC", "PC", "PC"),
("h_g_map_pc", "PC: only the PC players have this map; the PS2 players cannot play it.",
 "PC : seuls les joueurs sur PC ont cette carte ; les joueurs sur PS2 ne peuvent pas y jouer.",
 "PC: nur die PC-Spieler haben diese Karte; die PS2-Spieler können sie nicht spielen.",
 "PC: solo i giocatori su PC hanno questa mappa; i giocatori su PS2 non possono giocarla.",
 "PC: solo los jugadores de PC tienen este mapa; los jugadores de PS2 no pueden jugarlo.",
 "PC: só os jogadores de PC têm este mapa; os jogadores de PS2 não podem jogá-lo.",
 "ПК: эта карта есть только у игроков на ПК; игроки на PS2 не могут на ней играть.",
 "PC：このマップは PC のプレイヤーだけが持っています。PS2 のプレイヤーは遊べません。",
 "PC: 이 맵은 PC 플레이어만 가지고 있습니다. PS2 플레이어는 플레이할 수 없습니다.",
 "PC：只有 PC 玩家拥有此地图；PS2 玩家无法游玩。", "PC：只有 PC 玩家擁有此地圖；PS2 玩家無法遊玩。"),
]
