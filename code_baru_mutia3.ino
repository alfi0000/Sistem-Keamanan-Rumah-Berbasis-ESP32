#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// ========== KONFIGURASI ==========
#define SOLENOID_PIN    13
#define BLUE_LED_PIN    2
#define INFRARED_PIN    14
#define BUZZER_PIN      15

const char* WIFI_SSID     = "Mutia zulfa";
const char* WIFI_PASSWORD = "mutia1234";
const char* BOT_TOKEN     = "7731238680:AAH-Dfb3weTS2mjZbRebsNFMZioyuRwYDPI";
const char* CHAT_ID       = "7921292946";

// ========== OBJEK ==========
HardwareSerial fingerSerial(2);
Adafruit_Fingerprint finger(&fingerSerial);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// ========== KONSTANTA ==========
const int MIN_ID = 1;
const int MAX_ID = 20;
const unsigned long DOOR_OPEN_DURATION = 3000;
const unsigned long BOT_INTERVAL = 100;
const unsigned long INFRARED_INTERVAL = 100;

// ========== VARIABEL ==========
unsigned long lastBotCheck = 0;
unsigned long lastInfraredCheck = 0;
unsigned long doorOpenTime = 0;
bool doorOpen = false;
bool waitingToEnroll = false;
int idToEnroll = -1;
bool gerakanTerdeteksi = false;

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  fingerSerial.begin(57600, SERIAL_8N1, 16, 17);
  Wire.begin(21, 22);

  pinMode(SOLENOID_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  pinMode(INFRARED_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW);
  digitalWrite(BLUE_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Memulai...");

  finger.begin(57600);
  if (finger.verifyPassword()) {
    lcd.setCursor(0, 1); lcd.print("Sensor OK");
    Serial.println("[INFO] Sensor sidik jari terdeteksi.");
  } else {
    lcd.setCursor(0, 1); lcd.print("Sensor gagal");
    Serial.println("[ERROR] Gagal konek sensor.");
    while (true);
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("Koneksi WiFi...");
  while (WiFi.status() != WL_CONNECTED) delay(500);
  lcd.setCursor(0, 1); lcd.print("Tersambung!");
  Serial.println("[INFO] WiFi Tersambung.");

  sendNotification("🔔 Sistem siap digunakan.");
}

// ========== LOOP ==========
void loop() {
  unsigned long now = millis();

  if (!waitingToEnroll) {
    if (finger.getImage() == FINGERPRINT_OK &&
        finger.image2Tz() == FINGERPRINT_OK &&
        finger.fingerSearch() == FINGERPRINT_OK) {
      bukaPintu(finger.fingerID);
      delay(1000);
    }
  } else {
    enrollFingerprint(idToEnroll);
  }

  if (doorOpen && now - doorOpenTime >= DOOR_OPEN_DURATION) {
    digitalWrite(SOLENOID_PIN, LOW);
    doorOpen = false;
    lcd.clear(); lcd.setCursor(0, 0); lcd.print("Pintu tertutup");
    Serial.println("[INFO] Pintu tertutup otomatis.");
  }

  // Infrared deteksi
  if (now - lastInfraredCheck >= INFRARED_INTERVAL) {
    lastInfraredCheck = now;
    bool infraredState = digitalRead(INFRARED_PIN);

    if (infraredState == LOW && !gerakanTerdeteksi && !doorOpen) {
      gerakanTerdeteksi = true;
      bunyiAlarm();
      sendNotification("🚨 Gerakan terdeteksi saat pintu terkunci!");
      Serial.println("[ALARM] Gerakan terdeteksi!");
    }

    if (infraredState == HIGH) {
      gerakanTerdeteksi = false;
    }
  }

  // Periksa pesan dari bot
  if (now - lastBotCheck >= BOT_INTERVAL) {
    lastBotCheck = now;
    int messages = bot.getUpdates(bot.last_message_received + 1);
    if (messages > 0) handleTelegramMessages(messages);
  }
}

// ========== AKSI ==========
void bukaPintu(int id) {
  digitalWrite(SOLENOID_PIN, HIGH);
  doorOpen = true;
  doorOpenTime = millis();
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("Akses diberikan");
  lcd.setCursor(0, 1); lcd.print("ID: " + String(id));
  sendNotification("✅ Akses diberikan untuk ID #" + String(id));
  Serial.println("[AKSES] ID valid: " + String(id));
}

void sendNotification(String pesan) {
  bot.sendMessage(CHAT_ID, pesan, "");
}

void bunyiAlarm() {
  unsigned long startTime = millis();
  while (millis() - startTime < 3000) {
    tone(BUZZER_PIN, 1000); delay(200);
    noTone(BUZZER_PIN);     delay(200);
  }
}

// ========== DAFTAR SIDIK JARI ==========
void enrollFingerprint(int id) {
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("Tempel jari...");
  Serial.println("[DAFTAR] Menunggu jari untuk ID " + String(id));

  // Kedipkan LED saat menunggu jari pertama
  unsigned long start = millis();
  while (finger.getImage() != FINGERPRINT_OK) {
    digitalWrite(BLUE_LED_PIN, (millis() / 300) % 2);  // Kedip tiap 300ms
    if (millis() - start > 10000) {
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("Timeout!");
      Serial.println("[ERROR] Timeout jari pertama");
      digitalWrite(BLUE_LED_PIN, LOW);
      waitingToEnroll = false;
      return;
    }
    delay(50);
  }
  digitalWrite(BLUE_LED_PIN, HIGH);  // LED tetap nyala saat proses

  // Citra pertama
  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    lcd.setCursor(0, 1); lcd.print("Ulangi jari!");
    Serial.println("[ERROR] Gagal citra 1");
    digitalWrite(BLUE_LED_PIN, LOW);
    delay(1000);
    return;
  }

  lcd.setCursor(0, 1); lcd.print("Angkat jari...");
  Serial.println("[INFO] Angkat jari...");
  delay(1000);

  // Tunggu jari diangkat
  start = millis();
  while (finger.getImage() != FINGERPRINT_NOFINGER) {
    if (millis() - start > 10000) {
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("Timeout!");
      Serial.println("[ERROR] Timeout angkat jari");
      digitalWrite(BLUE_LED_PIN, LOW);
      waitingToEnroll = false;
      return;
    }
    delay(50);
  }

  // Scan ulang
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("Scan ulang jari");
  Serial.println("[INFO] Tunggu citra 2...");
  start = millis();
  while (finger.getImage() != FINGERPRINT_OK) {
    digitalWrite(BLUE_LED_PIN, (millis() / 300) % 2);  // Kedip lagi
    if (millis() - start > 10000) {
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("Timeout!");
      Serial.println("[ERROR] Timeout jari kedua");
      digitalWrite(BLUE_LED_PIN, LOW);
      waitingToEnroll = false;
      return;
    }
    delay(50);
  }
  digitalWrite(BLUE_LED_PIN, HIGH);  // LED tetap nyala saat proses

  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    lcd.setCursor(0, 1); lcd.print("Citra 2 gagal!");
    Serial.println("[ERROR] Gagal citra 2");
    digitalWrite(BLUE_LED_PIN, LOW);
    delay(1000);
    return;
  }

  // Buat dan simpan model
  if (finger.createModel() != FINGERPRINT_OK || finger.storeModel(id) != FINGERPRINT_OK) {
    lcd.setCursor(0, 1); lcd.print("Gagal simpan!");
    Serial.println("[ERROR] Gagal simpan ID " + String(id));
    bot.sendMessage(CHAT_ID, "❌ Gagal simpan ID " + String(id), "");
    digitalWrite(BLUE_LED_PIN, LOW);
  } else {
    lcd.setCursor(0, 1); lcd.print("Berhasil simpan!");
    Serial.println("[SUKSES] ID " + String(id) + " berhasil disimpan.");
    bot.sendMessage(CHAT_ID, "✅ ID " + String(id) + " berhasil didaftarkan", "");

    // Buzzer sukses
    tone(BUZZER_PIN, 1500, 200);  // bunyi 200ms
    delay(250);
    noTone(BUZZER_PIN);
  }

  digitalWrite(BLUE_LED_PIN, LOW);
  waitingToEnroll = false;
}


// ========== HANDLE TELEGRAM ==========
void handleTelegramMessages(int total) {
  for (int i = 0; i < total; i++) {
    String text = bot.messages[i].text;
    String chat_id = bot.messages[i].chat_id;

    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "❌ Akses ditolak", "");
      continue;
    }

    if (text == "/start") {
      String msg = "📋 Perintah:\n";
      msg += "/DaftarSidikJari - Daftar jari baru\n";
      msg += "/HapusSidikJari <ID>\n";
      msg += "/StatusSidikJari\n";
      msg += "/OpenDoor\n";
      msg += "/CloseDoor";
      bot.sendMessage(chat_id, msg, "");
    } else if (text == "/OpenDoor") {
      digitalWrite(SOLENOID_PIN, HIGH);
      doorOpen = true;
      doorOpenTime = millis();
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("Pintu dibuka");
      bot.sendMessage(chat_id, "🔓 Pintu dibuka", "");
    } else if (text == "/CloseDoor") {
      digitalWrite(SOLENOID_PIN, LOW);
      doorOpen = false;
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("Pintu tertutup");
      bot.sendMessage(chat_id, "🔒 Pintu ditutup", "");
    } else if (text.startsWith("/HapusSidikJari")) {
      int id = text.substring(16).toInt();
      if (id >= MIN_ID && id <= MAX_ID) {
        if (finger.deleteModel(id) == FINGERPRINT_OK) {
          bot.sendMessage(chat_id, "🗑️ ID " + String(id) + " dihapus", "");
          Serial.println("[INFO] ID " + String(id) + " dihapus.");
        } else {
          bot.sendMessage(chat_id, "❌ Gagal hapus ID " + String(id), "");
        }
      } else {
        bot.sendMessage(chat_id, "⚠️ ID harus antara 1-20", "");
      }
    } else if (text == "/DaftarSidikJari") {
      for (int id = MIN_ID; id <= MAX_ID; id++) {
        if (finger.loadModel(id) != FINGERPRINT_OK) {
          idToEnroll = id;
          waitingToEnroll = true;
          bot.sendMessage(chat_id, "👉 Tempelkan jari untuk ID " + String(id), "");
          break;
        }
      }
    } else if (text == "/StatusSidikJari") {
      String daftar = "📌 ID Terdaftar:\n";
      for (int id = MIN_ID; id <= MAX_ID; id++) {
        if (finger.loadModel(id) == FINGERPRINT_OK) {
          daftar += "- ID " + String(id) + "\n";
        }
      }
      bot.sendMessage(chat_id, daftar, "");
    }
  }
}
