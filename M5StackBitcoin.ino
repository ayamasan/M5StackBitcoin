#include <M5Stack.h>
#include "ArduinoJson.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <FS.h>
#include <SPIFFS.h>
#define FILENAME "/param.txt"
#define LOGNAME "/log.txt"


const char *ssid = "xxxxxxx";
const char *password = "xxxxxxx";
// ビットコイン値段を取得
const char *apiServer = "https://api.coindesk.com/v1/bpi/currentprice/JPY.json";

// LINE用
const char* host     = "notify-api.line.me";
const char* token    = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
int sendline = 0;

int inputif = 0;  // 数字入力オンオフ
int keta = 9;     // 数字入力の桁数
long number = 0;  // 入力数字
int num = 0;      // 選択中の数字
int ok = 0;       // 数字入力確定、キャンセル
unsigned long displayTime = 0;  // 長押し判定用
int numin = 0;    // 数字入力状態 0=無選択、1=Bボタン押、2=長押状態、3=確定/キャンセル選択中
int mode = 0;     // 数値表示 0=右詰め、1=左詰め
//int ketanum = 0;  // 浮動小数点入力時の入力桁数

int setupin = 0;  // 設定選択
int setupcol = BLACK;  // 設定中の文字色
int wifion = 0;   // WiFi接続状態
int nowyen = 50000;  // 所有額（円換算）
double mybit = 0.00538737;  // 所有ビットコイン
int sellyen = 55000; // 売り価格トリガ
int buyyen = 45000;  // 買い価格トリガ

#define LOGINTERVAL 60000 // ログ周期（1分）
int lineok = 0;   // ライン通知 1=オン
int graph = 0;    // グラフ表示モード 1=オン
int yenlog[300];  // 1分間隔で5時間分のログ
int lognum = 0;
unsigned long LogTime = 0;  // ログ記録周期用


// セットアップ
void setup() {
	// put your setup code here, to run once:
	M5.begin();
	M5.Lcd.setCursor(0, 0);
	M5.Lcd.fillScreen(BLACK); // 表示クリア
	
	// WiFi接続
	WiFi.begin(ssid, password);
	int i = 0;
	while(WiFi.status() != WL_CONNECTED){
		delay(100);
		i++;
		if(i>100){  // 10secタイムアウト
			M5.Lcd.setTextColor(BLACK, RED);  // 文字色,背景色
			M5.Lcd.println(" ERROR : cannot connect to WiFi. ");
			break;
		}
	}
	// WiFi接続成功
	if(WiFi.status() == WL_CONNECTED){
		wifion = 1;
	}
	
	SPIFFS.begin();
	delay(50);
	
	if(loadall() == 0){
		saveall();
		delay(100);
		loadall();
	}
	
	for(i=0; i<300; i++){
		yenlog[i] = 0;
	}
	
	// ログ読み込み
	File fp;
	fp = SPIFFS.open(LOGNAME, FILE_READ);
	if(fp){
		fp.readBytes((char *)&yenlog[0], sizeof(yenlog));
		fp.close();
	}
	
	initdisp(wifion, nowyen, mybit, sellyen, buyyen);
	
	if(wifion == 1){
		// ビットコインチェックタスク起動
		xTaskCreatePinnedToCore(BitcoinTask, "BitcoinTask", 4096, NULL, 1, NULL, 1);
	}
}


// ライン NOTIFICATION 発行
boolean line_notify(String msg) 
{
	WiFiClientSecure client;
	client.setInsecure();
	if(!client.connect(host, 443)){
		Serial.println("connect error!");
		return false;
	}
	String query = String("message=") + msg;
	String request = String("")
		+ "POST /api/notify HTTP/1.1\r\n"
		+ "Host: " + host + "\r\n"
		+ "Authorization: Bearer " + token + "\r\n"
		+ "Content-Length: " + String(query.length()) +  "\r\n"
		+ "Content-Type: application/x-www-form-urlencoded\r\n\r\n"
		+ query + "\r\n";
	client.print(request);
	return true;
}


// ビットコインログ記録（graph用）
void BitLogTask() 
{
	File fp;
	int i;
	
	for(i=0; i<299; i++){
		yenlog[i] = yenlog[i+1];
	}
	if(wifion == 2){
		yenlog[299] = nowyen;
	}
	else{
		yenlog[299] = 0;
	}
	if(graph != 0){
		Serial.printf("yenlog[] <-- %d\n", yenlog[287]);
	}
	
	if(lognum == 300){
		// 保存（ログが300=5時間たまる毎に保存）
		fp = SPIFFS.open(LOGNAME, FILE_WRITE);
		fp.write((uint8_t *)&yenlog[0], sizeof(yenlog));
		fp.close();
		lognum = 0;
		Serial.println(">>> SAVE LOG");
	}
	else if(lognum < 300){
		lognum++;
	}
}
	

// ビットコインチェック用タスク
void BitcoinTask(void* arg) 
{
	int timeout = 0;
	int yen;
	int httpCode;
	double rate;
	JsonObject obj;
	JsonObject result;
	String msg;
	// json用メモリ確保
	DynamicJsonDocument doc(1024);
	
	Serial.println("Start BitcoinTask.");
	
	while(1){
		if(setupin == 0){
			if((WiFi.status() == WL_CONNECTED)){
				timeout = 0;
				HTTPClient http;
				
				// HTTP接続開始
				http.begin(apiServer);
				
				// リクエスト作成
				httpCode = http.GET();
				//Serial.println(httpCode);
				
				// 返信
				if(httpCode > 0){
					// 応答取得
					String payload = http.getString();
					// ペイロードをjson変換
					deserializeJson(doc, payload);
					obj = doc.as<JsonObject>();
					// bpi.JPY
					result = obj[String("bpi")][String("JPY")];
					// ビットコイン表示更新
					rate = result[String("rate_float")];
					yen = nowyen;
					nowyen = (int)(mybit * rate);
					if(nowyen != yen || wifion != 2){
						wifion = 2;
						initdisp(wifion, nowyen, mybit, sellyen, buyyen);
						Serial.printf("Bitcoin %d yen\n", nowyen);
					}
					
					if(nowyen > sellyen || nowyen < buyyen){
						if(sendline == 0 && lineok != 0){
							lineok = 0;
							sendline = 1;
							// ラインへ送信
							if(nowyen > sellyen){
								msg = "ビットコイン：" + String(nowyen) + "円を超えました。";
							}
							else{
								msg = "ビットコイン：" + String(nowyen) + "円を下回りました。";
							}
							line_notify(msg);
							
							saveall();
							initdisp(wifion, nowyen, mybit, sellyen, buyyen);
						}
					}
				}
				else{
					if(wifion != 1){
						wifion = 1;
						initdisp(wifion, nowyen, mybit, sellyen, buyyen);
					}
				}
				
				http.end();
			}
			else{
				if(wifion != 0){
					wifion = 0;
					initdisp(wifion, nowyen, mybit, sellyen, buyyen);
				}
				timeout++;
				if(timeout >= 3){  // 15秒WiFi切断 = WiFi再起動
					// WiFi切断
					WiFi.disconnect();
					vTaskDelay(1000);
					// WiFi再接続
					WiFi.begin(ssid, password);
					timeout = 0;
					Serial.println("Reconnect WiFi.");
				}
			}
			vTaskDelay(5000);
		}
		else{
			vTaskDelay(1000);
		}
	}
	
	//Serial.println("STOP BitcoinTask.");
	vTaskDelay(10);
	// タスク削除
	vTaskDelete(NULL);
}


// 画面表示更新
void initdisp(int wifi, int now, double bit, int sell, int buy)
{
	int x = 320-30*7-20;  // 最大7桁
	int y = 0;
	
	M5.Lcd.fillScreen(BLACK); // 表示クリア
	
	// ライン通知表示
	if(sendline != 0){
		M5.Lcd.setTextFont(4);
		M5.Lcd.setCursor(96, 210);
		M5.Lcd.setTextColor(BLACK, GREEN);
		M5.Lcd.print(" SEND LINE ");
	}
	
	// ラインオンオフ
	if(lineok != 0){
		M5.Lcd.fillRect(310, 0, 10, 10, GREEN);
	}
	
	// WiFi接続状態
	M5.Lcd.setTextFont(2);
	M5.Lcd.setCursor(0, y);
	if(wifi == 0){
	M5.Lcd.setTextColor(RED, BLACK);
		M5.Lcd.println(" WiFi none.");
	}
	else if(wifi == 1){
		M5.Lcd.setTextColor(RED, BLACK);
		M5.Lcd.println(" Server none.");
	}
	else{
		M5.Lcd.setTextColor(BLUE, BLACK);
		M5.Lcd.println(" WiFi OK");
	}
	
	if(graph == 0){  // グラフ表示中でない
		y += 24;
		// 現在の価格（7桁）
		M5.Lcd.setCursor(10, y);
		M5.Lcd.setTextFont(2);
		if(wifi == 0){
			M5.Lcd.setTextColor(LIGHTGREY, BLACK);
		}
		else{
			M5.Lcd.setTextColor(GREEN, BLACK);
		}
		M5.Lcd.print("now yen");
		M5.Lcd.setCursor(x, y);
		M5.Lcd.setTextFont(7);
		M5.Lcd.printf("%d\n", now);
		y += 50;
		// 所有ビットコイン
		M5.Lcd.setCursor(10, y);
		M5.Lcd.setTextFont(2);
		M5.Lcd.setTextColor(LIGHTGREY, BLACK);
		M5.Lcd.printf("my bitcoin %.8f BTC\n", bit);
		y += 20;
		// 売り設定
		M5.Lcd.setCursor(10, y);
		M5.Lcd.setTextFont(2);
		M5.Lcd.setTextColor(CYAN, BLACK);
		M5.Lcd.print("sell notice");
		M5.Lcd.setTextFont(7);
		M5.Lcd.setCursor(x, y);
		M5.Lcd.printf("%d\n", sell);
		y += 50;
		// 買い設定
		M5.Lcd.setCursor(10, y);
		M5.Lcd.setTextFont(2);
		M5.Lcd.setTextColor(RED, BLACK);
		M5.Lcd.print("buy notice");
		M5.Lcd.setTextFont(7);
		M5.Lcd.setCursor(x, y);
		M5.Lcd.printf("%d\n", buy);
		y += 50;
		
		keta = 9;
		//inputdisp(0, GREEN, 0, 0, " Number Input Demo ");
	}
	else{  // graph グラフエリア：y=20..210
		M5.Lcd.fillRect(10, 20, 300, 190, DARKGREY);
		//wifion, nowyen, mybit, sellyen, buyyen
		x = 10;
		int i;
		int yy;
		int yyold;
		int yold;
		int col = LIGHTGREY;
		for(i=0; i<=300; i+=60){
			M5.Lcd.drawLine(i+10, 20, i+10, 210, LIGHTGREY);  // 1時間
		}
		M5.Lcd.drawLine(10, 20, 310, 20, CYAN);  // 売り
		M5.Lcd.drawLine(10, 210, 310, 210, RED); // 買い
		
		yy = 210;
		yyold = 210;
		for(i=0; i<300; i++){
			col = LIGHTGREY;
			y = yenlog[i];
			if(y == 0){
				if(yold > 0){
					y = yold;
				}
			}
			if(y > 0){
				col = GREEN;
				yold = y;
			}
			
			if(yenlog[i] >= buyyen && yenlog[i] <= sellyen){
				yy = 210 - (int)(((double)yenlog[i] - buyyen) * (190.0 / (double)(sellyen - buyyen)));
				if(i==299){
					Serial.printf("graph yy = %d\n", yy);
				}
				//M5.Lcd.drawPixel(x, yy, col);
				if(i > 0){
					M5.Lcd.drawLine(x-1, yyold, x, yy, GREEN);
				}
				yyold = yy;
			}
			x++;
		}
		// 現在価格
		M5.Lcd.setTextFont(2);
		M5.Lcd.setCursor(140, 0);
		M5.Lcd.setTextColor(GREEN, BLACK);
		M5.Lcd.printf(" %d yen ", nowyen);
	}
}


// 設定メニュー表示
void setupdisp(int sel)
{
	int x = 0;
	M5.Lcd.setTextFont(4);
	
	M5.Lcd.setCursor(x, 210);
	if(sel == 1){
		M5.Lcd.setTextColor(BLACK, WHITE);
	}
	else{
		M5.Lcd.setTextColor(WHITE, LIGHTGREY);
	}
	M5.Lcd.print("SELL");
	x += 64;
	
	M5.Lcd.setCursor(x, 210);
	if(sel == 2){
		M5.Lcd.setTextColor(BLACK, WHITE);
	}
	else{
		M5.Lcd.setTextColor(WHITE, LIGHTGREY);
	}
	M5.Lcd.print("BUY");
	x += 56;
	
	M5.Lcd.setCursor(x, 210);
	if(sel == 3){
		M5.Lcd.setTextColor(BLACK, WHITE);
	}
	else{
		M5.Lcd.setTextColor(WHITE, LIGHTGREY);
	}
	M5.Lcd.print("BTC");
	x += 54;
	
	M5.Lcd.setCursor(x, 210);
	if(sel == 4){
		M5.Lcd.setTextColor(BLACK, WHITE);
	}
	else{
		M5.Lcd.setTextColor(WHITE, LIGHTGREY);
	}
	M5.Lcd.print("CLOSE");
	x += 88;
	
	M5.Lcd.setCursor(x, 210);
	if(lineok == 0){
		if(sel == 5){
			M5.Lcd.setTextColor(BLACK, WHITE);
		}
		else{
			M5.Lcd.setTextColor(WHITE, LIGHTGREY);
		}
	}
	else{
		M5.Lcd.setTextColor(BLACK, GREEN);
	}
	M5.Lcd.print("LINE");
}


// SPIFFS保存
void saveall()
{
	File fp;
	
	Serial.println("SPIFFS file save.");
	// フォーマット
	//SPIFFS.format();
	// ファイルを開く
	fp = SPIFFS.open(FILENAME, FILE_WRITE);
	// 書き込み
	fp.write((uint8_t *)&lineok, sizeof(lineok));
	fp.write((uint8_t *)&sellyen, sizeof(sellyen));
	fp.write((uint8_t *)&buyyen, sizeof(buyyen));
	fp.write((uint8_t *)&mybit, sizeof(mybit));
	
	fp.close();
	Serial.println("SPIFFS witre complete.");
	
	Serial.printf("linkok=%d, sell=%d, buy=%d, BTC=%.8f\n"
	, lineok, sellyen, buyyen, mybit);
}


// SPIFFS読み込み
int loadall()
{
	File fp;
	int ss = 0;
	
	fp = SPIFFS.open(FILENAME, FILE_READ);
	if(!fp){
		Serial.println("ERROR : SPIFFS file open error.");
		return(0);
	}
	size_t size = fp.size();
	Serial.printf("SPIFFS file size = %d\n", size);
	if(size < 20){
		Serial.printf("ERROR : File size = %d < 20\n", size);
		return(0);
	}
	
	// 読み込み
	fp.readBytes((char *)&lineok, 4);
	fp.readBytes((char *)&sellyen, 4);
	fp.readBytes((char *)&buyyen, 4);
	fp.readBytes((char *)&mybit, 8);
	/*
	lineok   // int(4)
	sellyen  // int(4)
	buyyen   // int(4)
	mybit    // double(8)
	*/
	
	fp.close();
	Serial.println("SPIFFS read complete.");
	
	Serial.printf("linkok=%d, sell=%d, buy=%d, BTC=%.8f\n"
	, lineok, sellyen, buyyen, mybit);
	
	return(size);
}


// メインルーチン
void loop() 
{
	int i, j;
	long k;
	
	// put your main code here, to run repeatedly:
	M5.update();
	
	if(inputif == 0){  // 設定選択
		// Bボタン
		if(M5.BtnB.wasReleased()){
			Serial.println("[B] button!");
			if(setupin == 0){  // セットアップ開始
				if(graph == 0){  // グラフ表示中でない
					sendline = 0;
					setupin = 4;   // CLOSE
					setupcol = BLACK;
					setupdisp(setupin);
				}
			}
			else{
				if(setupin == 4){  // セットアップ終了
					Serial.println("Setup complete.");
					setupin = 0;
					saveall();
					initdisp(wifion, nowyen, mybit, sellyen, buyyen);
				}
				else if(setupin == 5){  // LINE
					if(lineok == 0){
						lineok = 1;
					}
					else{
						lineok = 0;
					}
					saveall();
					setupdisp(setupin);
				}
				else if(setupin == 1){  // SELL
					inputif = 1;
					number = (long)sellyen;
					num = 0;
					keta = 7;
					setupcol = CYAN;
					mode = 0;
					inputdisp(mode, setupcol, 0, 0, " SELL TRIGGER YEN ");
					numin = 0;
				}
				else if(setupin == 2){  // BUY
					inputif = 2;
					number = (long)buyyen;
					num = 0;
					keta = 7;
					setupcol = RED;
					mode = 0;
					inputdisp(mode, setupcol, 0, 0, " BUY TRIGGER YEN ");
					numin = 0;
				}
				else if(setupin == 3){  // BIT
					inputif = 3;
					number = (long)(mybit * 100000000);
					num = 0;
					keta = 8;
					setupcol = GREEN;
					mode = 1;
					inputdisp(mode, setupcol, 0, 0, " MY BITCOIN ");
					numin = 0;
				}
			}
		}
		// Aボタン
		if(M5.BtnA.wasPressed()){
			Serial.println("[A] button!");
			if(setupin > 1){
				setupin--;
				setupdisp(setupin);
			}
			else{
				// グラフ表示
				if(graph == 0){
					Serial.println("GRAPH mode");
					graph = 1;
				}
				else{
					Serial.println("TEXT mode");
					graph = 0;
				}
				initdisp(wifion, nowyen, mybit, sellyen, buyyen);
			}
		}
		// Cボタン
		if(M5.BtnC.wasPressed()){
			Serial.println("[C] button!");
			if(setupin > 0 && setupin < 5){
				setupin++;
				setupdisp(setupin);
			}
		}
	}
	else{  // 数字入力中
		// Aボタン
		if(M5.BtnA.wasPressed()){
			if(numin == 0){
				if(num >= 0){
					select(num, num-1);
					num--;
				}
			}
			else if(numin == 3){
				ok = 0;
				okdisp(ok);
			}
		}
		
		// Cボタン
		if(M5.BtnC.wasPressed()){
			if(numin == 0){
				if(num < 9){
					select(num, num+1);
					num++;
				}
			}
			else if(numin == 3){
				ok = 1;
				okdisp(ok);
			}
		}
		
		// Bボタン
		if(M5.BtnB.wasPressed()){
			if(numin >= 2){
				
			}
			else{
				displayTime = millis();
				numin = 1;
			}
		}
		if(M5.BtnB.wasReleased()){
			if(numin == 1){  // 数字入力 or 1文字削除
				if(mode == 0){
					if(num >= 0){  // 数字追加
						if(number < pow(10, keta-1)){
							number = (number * 10) + num;
							numdisp(30+(9-keta)*30, 40, number, keta, setupcol, setupcol, mode);
						}
					}
					else{  // 1文字削除
						number = number / 10;
						numdisp(30+(9-keta)*30, 40, number, keta, setupcol, setupcol, mode);
					}
				}
				else{
					if(num >= 0){  // 数字追加
						if(number < pow(10, keta)){
							number = (number * 10) + num;
							numdisp(36+(9-keta)*30, 40, number, keta, setupcol, setupcol, mode);
						}
					}
					else{  // 1文字削除
						if(number > 1){
							number = number / 10;
							numdisp(36+(9-keta)*30, 40, number, keta, setupcol, setupcol, mode);
						}
					}
				}
				numin = 0;
			}
			else if(numin == 2){
				numin++;
			}
			else if(numin == 3){
				if(ok == 0){
					switch(setupin){
						case 1 :  // SELL
							if((int)number > buyyen){
								sellyen = (int)number; 
							}
							break;
						case 2 :  // BYE
							if((int)number < sellyen){
								buyyen = (int)number;
							}
							break;
						case 3 : // BIT（小数点以下8桁）
							// 9桁に揃える（右に0を入れる）
							// 現在の桁数を求める
							k = 10;
							j = keta;
							for(i=0; i<keta; i++){
								if((number / k) == 0){
									j = i;
									break;
								}
								k *= 10;
							}
							k = number;
							for(i=j; i<keta; i++){
								k *= 10;
							}
							number = k - 100000000;
							mybit = (double)number / 100000000.0;
							break;
						default : break;  // CANCEL
					}
					saveall();
				}
				
				initdisp(wifion, nowyen, mybit, sellyen, buyyen);
				inputif = 0;
				setupin = 0;
				number = 0;
				num = 0;
				keta = 9;
				//inputdisp(0, GREEN, 0, 0, " Number Input Demo ");
				numin = 0;
			}
		}
		
		// 長押し判定
		if(numin == 1){
			if(millis() - displayTime >= 1000){
				ok = 0;
				okdisp(ok);
				numin = 2;
			}
		}
	}
	
	// ログ
	if(millis() - LogTime >= LOGINTERVAL){
		BitLogTask();
		if(graph != 0){
			initdisp(wifion, nowyen, mybit, sellyen, buyyen);
		}
		LogTime = millis();
	}
	
	delay(10);
}


// 数字入力表示
void inputdisp(int mode, int col, int x, int y, char *str)
{
	int i;
	
	M5.Lcd.fillScreen(BLACK);   // 表示クリア
	
	// (x,y)にstr文字を表示
	M5.Lcd.setTextFont(4);
	M5.Lcd.setTextColor(col, BLACK);
	M5.Lcd.setCursor(x, y);
	M5.Lcd.print(str);
	
	// 文字入力エリア
	M5.Lcd.setTextFont(7);
	if(mode == 0){
		M5.Lcd.setTextColor(BLACK, col);
		numdisp(30+(9-keta)*30, 40, number, keta, setupcol, setupcol, mode);
	}
	else{
		// 「0.」表示
		M5.Lcd.setTextColor(setupcol, BLACK);
		M5.Lcd.drawNumber(0, 10, 40, 7);
		M5.Lcd.fillRect(42, 86, 4, 4, setupcol);
		
		M5.Lcd.setTextColor(BLACK, col);
		number += 100000000;
		numdisp(36+(9-keta)*30, 40, number, keta, setupcol, setupcol, mode);
	}
	
	// 数字選択エリア
	for(i=-1; i<10; i++){
		if(num == i){  // 選択中
			M5.Lcd.setTextColor(BLACK, WHITE);
		}
		else{
			M5.Lcd.setTextColor(DARKGREY, LIGHTGREY);
		}
		if(i >= 0){
			M5.Lcd.drawNumber(i, 10+30*i, 240-48, 7);
		}
		else{
			M5.Lcd.setTextFont(4);
			M5.Lcd.setCursor(10, 240-80);
			M5.Lcd.print("[DEL]");
		}
	}
}


// 数字選択描画
// on=選択数字、off=解除数字
void select(int off, int on)
{
	M5.Lcd.setTextColor(DARKGREY, LIGHTGREY);
	if(off >= 0){  // 数字
		M5.Lcd.drawNumber(off, 10+30*off, 240-48, 7);
	}
	else{  // DEL
		M5.Lcd.setTextFont(4);
		M5.Lcd.setCursor(10, 240-80);
		M5.Lcd.print("[DEL]");
	}
	M5.Lcd.setTextColor(BLACK, WHITE);
	if(on >= 0){  // 数字
		M5.Lcd.drawNumber(on, 10+30*on, 240-48, 7);
	}
	else{  // DEL
		M5.Lcd.setTextFont(4);
		M5.Lcd.setCursor(10, 240-80);
		M5.Lcd.print("[DEL]");
	}
}


// 数字入力エリア描画
// 右上指定座標(x, y)から9文字分(幅270、高48+2）のエリアに数字描画
// mode : 0=数字は右詰め、1=左詰め（最大9桁）
void numdisp(int x, int y, long num, int keta, int col, int cur, int mode)
{
	// 数字エリア消去
	M5.Lcd.fillRect(x, y, 320-x, 48+2, BLACK);
	
	// 下線
	if(cur != BLACK){
		M5.Lcd.fillRect(x, y+48, keta*30, 2, col);
	}
	// フォント7は、30×48画素
	int i;
	int j;
	long k = 0;
	long div;
	int m;
	if(mode == 0){
		div = pow(10, keta-1);
		for(i=0; i<keta; i++){
			j = (num / div) % 10;
			if(j != 0){   // はじめて1以上の数値が来た時から入力を認識
				k = 1;
			}
			if(j != 0 || k != 0){
				M5.Lcd.setTextColor(BLACK, col);
				M5.Lcd.drawNumber(j, x+30*i, y, 7);
			}
			div /= 10;
		}
	}
	else{
		// 現在の入力されている桁数を調べる
		k = 1;
		j = 0;
		for(i=0; i<=keta; i++){
			if(((long)num / k) == 0){
				i = keta+1;
			}
			else{
				k *= 10;
				j++;
			}
		}
		// 左詰めで数値を桁数分描画
		k /= 100;
		for(i=0; i<j-1; i++){
			if(k == 0){
				break;
			}
			m = ((long)num / k) % 10;
			M5.Lcd.setTextColor(BLACK, col);
			M5.Lcd.drawNumber(m, x+30*i, y, 7);
			k /= 10;
		}
	}
}


// 数字入力確定、キャンセル選択表示
// 0=ok, 1=cancel
void okdisp(int ok)
{
	// 数字選択表示消去
	M5.Lcd.fillRect(0, 240-80, 320, 80, BLACK);
	
	if(ok == 0){
		M5.Lcd.setTextColor(BLACK, WHITE);
	}
	else{
		M5.Lcd.setTextColor(DARKGREY, LIGHTGREY);
	}
	M5.Lcd.setCursor(30, 240-80);
	M5.Lcd.setTextFont(4);
	M5.Lcd.print(" [OK] ");
	
	if(ok != 0){
		M5.Lcd.setTextColor(BLACK, WHITE);
	}
	else{
		M5.Lcd.setTextColor(DARKGREY, LIGHTGREY);
	}
	M5.Lcd.setCursor(160, 240-80);
	M5.Lcd.setTextFont(4);      // フォント
	M5.Lcd.print(" [CANCEL] ");
}

