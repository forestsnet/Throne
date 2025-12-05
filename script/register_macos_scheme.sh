#!/bin/bash

APP_PATH="build/Throne.app"

if [ ! -d "$APP_PATH" ]; then
    echo "Приложение не найдено: $APP_PATH"
    echo "Сначала соберите проект!"
    exit 1
fi

echo "Регистрируем URL scheme для: $APP_PATH"

# Убиваем старые регистрации
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -kill -r -domain local -domain system -domain user

# Регистрируем заново
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -v "$APP_PATH"

echo ""
echo "Готово! Теперь throne:// URLs будут открываться в вашей версии"
echo ""
echo "Тест:"
open "throne://test"