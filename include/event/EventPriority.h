// include/event/EventPriority.h
#pragma once

namespace event {

enum class EventPriority : int {
    Papik = 30,      // сообщение от создателя
    Critical = 25,   // требующие немедленного ответа
    High = 20,       // закреплённые чаты, важные уведомления
    Normal = 10,     // обычные сообщения
    Low = 5,         // реакции, эмодзи
    Background = 0   // фоновые события
};

} // namespace event