#!/bin/bash

echo "🔄 Spájam časti menu.c1, menu.c2 a menu.c3..."
# Spojenie troch častí do výsledného menu.c
cat menu.c1 menu.c2 menu.c3 > menu.c

if [ $? -eq 0 ]; then
    echo "✅ Súbor menu.c bol úspešne vytvorený."
    echo "🛠️ Spúšťam kompiláciu cez Makefile..."
    
    # Spustenie tvojho makefile
    make
    
    if [ $? -eq 0 ]; then
        echo "🚀 Panel bol úspešne skompilovaný!"
    else
        echo "❌ Chyba pri kompilácii (make zlyhal)."
    fi
else
    echo "❌ Chyba pri spájaní súborov."
fi

