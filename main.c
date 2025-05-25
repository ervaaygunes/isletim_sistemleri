//
//  main.c
//  apartman2
//
//  Created by Erva Aygüneş Sude Naz Doğdu on 19.05.2025.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>

// Sabitler: Apartman toplam kat ve daire sayıları
#define KAT_SAYISI 10
#define DAIRE_SAYISI 4

// Ortak kaynaklar için senkronizasyon nesneleri
pthread_mutex_t vinç_mutex;       // Vinç gibi yalnızca bir dairenin kullanabileceği ortak kaynak
sem_t elektrik_sem;               // Elektrik hattı (aynı anda 2 thread çalışabilir)
sem_t su_sem;                     // Su hattı (aynı anda yalnızca 1 thread)
sem_t temel_sem;                  // Temel atıldıktan sonra inşaatın başlamasını kontrol eden semafor

// Katın tamamlandığını belirlemek için kullanılan koşul değişkenleri
pthread_mutex_t kat_tamamlama_mutex;
pthread_cond_t kat_tamamlandi_cond;
int tamamlanan_daire_sayisi = 0;  // O anki process'te tamamlanan daire sayısı

// Görev isimleri: her thread'e bir iş verilir
const char* görevler[] = {"Sıva", "Elektrik", "Su Tesisatı", "İç Tasarım"};

// Timestamp almak için yardımcı fonksiyon
void zaman_damgasi(char* buffer) {
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, 64, "%H:%M:%S", timeinfo);
}

// Daire inşaat süreci - Her thread bu fonksiyonu çalıştırır
void* daire_insa_et(void* arg) {
    int daire_no = *(int*)arg;
    char zaman[64];

    // Başlangıç logu
    zaman_damgasi(zaman);
    printf("[%s] Daire %d: İnşaat başladı.\n", zaman, daire_no);

    // Ortak vinç kullanımı (mutex ile kontrol)
    pthread_mutex_lock(&vinç_mutex);
    zaman_damgasi(zaman);
    printf("[%s] Daire %d: Vinç kullanıyor.\n", zaman, daire_no);
    sleep(1);
    zaman_damgasi(zaman);
    printf("[%s] Daire %d: Vinç işi bitti.\n", zaman, daire_no);
    pthread_mutex_unlock(&vinç_mutex);

    // Elektrik tesisatı (aynı anda en fazla 2 daire yapabilir)
    sem_wait(&elektrik_sem);
    zaman_damgasi(zaman);
    printf("[%s] Daire %d: Elektrik tesisatı başlıyor.\n", zaman, daire_no);
    sleep(1);
    zaman_damgasi(zaman);
    printf("[%s] Daire %d: Elektrik tesisatı tamamlandı.\n", zaman, daire_no);
    sem_post(&elektrik_sem);

    // Su tesisatı (aynı anda yalnızca 1 daire yapabilir)
    sem_wait(&su_sem);
    zaman_damgasi(zaman);
    printf("[%s] Daire %d: Su tesisatı başlıyor.\n", zaman, daire_no);
    sleep(1);
    zaman_damgasi(zaman);
    printf("[%s] Daire %d: Su tesisatı tamamlandı.\n", zaman, daire_no);
    sem_post(&su_sem);

    // Bağımsız görev: örneğin sıva, iç tasarım vs.
    int görev = daire_no % 4;
    zaman_damgasi(zaman);
    printf("[%s] Daire %d: %s işlemi başlıyor.\n", zaman, daire_no, görevler[görev]);
    sleep(1);
    zaman_damgasi(zaman);
    printf("[%s] Daire %d: %s işlemi tamamlandı.\n", zaman, daire_no, görevler[görev]);

    // Daire tamamlandığında, kat tamamlandı mı kontrol edilir
    pthread_mutex_lock(&kat_tamamlama_mutex);
    tamamlanan_daire_sayisi++;
    if (tamamlanan_daire_sayisi == DAIRE_SAYISI) {
        // Tüm daireler bittiğinde ana thread'e sinyal verilir
        pthread_cond_signal(&kat_tamamlandi_cond);
    }
    pthread_mutex_unlock(&kat_tamamlama_mutex);

    pthread_exit(NULL); // Thread sonlanır
}

int main() {
    // Senkronizasyon değişkenleri başlatılır
    pthread_mutex_init(&vinç_mutex, NULL);
    sem_init(&elektrik_sem, 1, 2);
    sem_init(&su_sem, 1, 1);
    sem_init(&temel_sem, 1, 0);

    pthread_mutex_init(&kat_tamamlama_mutex, NULL);
    pthread_cond_init(&kat_tamamlandi_cond, NULL);

    // Temel atma simülasyonu
    printf("Temel atılıyor...\n");
    sleep(2);
    sem_post(&temel_sem); // İnşaata başlanmasına izin verilir

    // Her kat için ayrı bir process oluşturulur
    for (int kat = 0; kat < KAT_SAYISI; kat++) {
        sem_wait(&temel_sem);
        sem_post(&temel_sem);

        pid_t pid = fork();

        if (pid < 0) {
            perror("Fork hatası");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // Çocuk process: Kendi katının inşaatını yönetir
            printf("=======================================\n");
            printf("KAT %d inşaatı başlıyor (PID: %d)\n", kat + 1, getpid());

            pthread_t daireler[DAIRE_SAYISI];
            int daire_numaralari[DAIRE_SAYISI];
            tamamlanan_daire_sayisi = 0;  // Sayaç sıfırlanmalı (child process için)

            // Her daire için bir thread başlat
            for (int i = 0; i < DAIRE_SAYISI; i++) {
                daire_numaralari[i] = (kat * DAIRE_SAYISI) + (i + 1); // Örn: daire 1–40 arası
                pthread_create(&daireler[i], NULL, daire_insa_et, &daire_numaralari[i]);
            }

            // Tüm thread'lerin tamamlanması beklenir
            for (int i = 0; i < DAIRE_SAYISI; i++) {
                pthread_join(daireler[i], NULL);
            }

            // Kat tamamlandı mı kontrolü
            pthread_mutex_lock(&kat_tamamlama_mutex);
            while (tamamlanan_daire_sayisi < DAIRE_SAYISI) {
                pthread_cond_wait(&kat_tamamlandi_cond, &kat_tamamlama_mutex);
            }
            pthread_mutex_unlock(&kat_tamamlama_mutex);

            // Kat bitiş logu
            printf("KAT %d tamamlandı (PID: %d)\n", kat + 1, getpid());
            printf("=======================================\n");

            exit(0); // Process sonlanır
        } else {
            // Ebeveyn process: Bu kat bitmeden sonraki kata geçmez
            wait(NULL);
        }
    }

    // Tüm kaynaklar serbest bırakılır
    pthread_mutex_destroy(&vinç_mutex);
    sem_destroy(&elektrik_sem);
    sem_destroy(&su_sem);
    sem_destroy(&temel_sem);
    pthread_mutex_destroy(&kat_tamamlama_mutex);
    pthread_cond_destroy(&kat_tamamlandi_cond);

    return 0;
}
