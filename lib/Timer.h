class Timer {
private:
    unsigned long startTime;   // момент последнего включения (мс)
    bool lastEnable;           // предыдущее состояние enable
    bool completed;            // флаг завершения интервала

public:
    // Конструктор по умолчанию
    Timer() : startTime(0), lastEnable(false), completed(false) {}

    // Основной метод: обновляет состояние таймера и возвращает true,
    // если интервал истёк при включённом состоянии.
    // Параметры:
    //   enable   - текущее состояние включения таймера
    //   interval - длительность интервала в миллисекундах (используется при enable = true)
    bool check(bool enable, unsigned long interval) {
        // При изменении состояния enable
        if (enable != lastEnable) {
            startTime = millis();          // запоминаем время переключения
            lastEnable = enable;
            if (!enable) {
                completed = false;          // сброс флага при выключении
            }
        }

        // Если таймер включён, ещё не завершён и прошло достаточно времени
        if (enable && !completed && (millis() - startTime) > interval) {
            completed = true;
        }

        return completed;
    }

    // Дополнительно: принудительный сброс таймера в исходное состояние
    void reset() {
        startTime = 0;
        lastEnable = false;
        completed = false;
    }

    // Получить оставшееся время (если таймер включён и не завершён)
    unsigned long remaining(unsigned long interval) const {
        if (!lastEnable || completed) return 0;
        unsigned long elapsed = millis() - startTime;
        return (elapsed >= interval) ? 0 : (interval - elapsed);
    }
};