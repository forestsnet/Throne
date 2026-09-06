#include "include/sys/macos/MacNotify.hpp"

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <QCoreApplication>
#include <QTimer>

#include <mutex>

namespace {
    // Пишется из потока Qt, читается из очереди UserNotifications.
    std::mutex g_activatedLock;
    std::function<void()> g_activated;

    std::function<void()> activationHandler() {
        std::lock_guard<std::mutex> guard(g_activatedLock);
        return g_activated;
    }


    // Колбэки UserNotifications приходят в своих очередях, а трогать окна можно
    // только из потока Qt.
    void toUiThread(const std::function<void()> &work) {
        if (!work) return;
        QTimer::singleShot(0, qApp, work);
    }
}

// Делегат нужен по двум причинам: без него уведомление не показывается, пока
// приложение активно (система считает, что человек и так смотрит в окно), и
// щелчок по баннеру никуда не приводит.
@interface ThroneNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation ThroneNotificationDelegate

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    Q_UNUSED(center)
    Q_UNUSED(notification)
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList |
                      UNNotificationPresentationOptionSound);
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
             withCompletionHandler:(void (^)(void))completionHandler {
    Q_UNUSED(center)
    if ([response.actionIdentifier isEqualToString:UNNotificationDefaultActionIdentifier]) {
        toUiThread(activationHandler());
    }
    completionHandler();
}

@end

namespace {
    // Центр уведомлений живёт до конца процесса, и делегат должен жить столько же:
    // отпустишь — щелчки по баннеру перестанут доходить.
    ThroneNotificationDelegate *g_delegate = nil;

    UNUserNotificationCenter *notificationCenter() {
        if (NSClassFromString(@"UNUserNotificationCenter") == nil) return nil;
        // Приложение без подписи и без записи в LaunchServices роняет здесь
        // исключение, а не возвращает nil.
        @try {
            UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
            if (center != nil && g_delegate == nil) {
                g_delegate = [[ThroneNotificationDelegate alloc] init];
                center.delegate = g_delegate;
            }
            return center;
        } @catch (NSException *exception) {
            NSLog(@"UNUserNotificationCenter unavailable: %@", exception.reason);
            return nil;
        }
    }

    // Всё по значению: блок уходит в чужую очередь и переживает вызывающий код.
    // Со ссылками на QString это стоило падения в usernotifications call-out.
    void deliver(UNUserNotificationCenter *center, QString title, QString body,
                 std::function<void()> onRefused) {
        UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
        content.title = title.toNSString();
        content.body = body.toNSString();
        content.sound = [UNNotificationSound defaultSound];

        UNNotificationRequest *request =
            [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
                                                 content:content
                                                 trigger:nil];
        const std::function<void()> refusedCopy = onRefused;
        [center addNotificationRequest:request
                 withCompletionHandler:^(NSError *error) {
                     if (error != nil) {
                         NSLog(@"notification refused: %@", error.localizedDescription);
                         toUiThread(refusedCopy);
                     }
                 }];
    }
}

namespace MacNotify {
    void Prime() {
        UNUserNotificationCenter *center = notificationCenter();
        if (center == nil) return;
        [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings *settings) {
            if (settings.authorizationStatus != UNAuthorizationStatusNotDetermined) return;
            [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                                  completionHandler:^(BOOL granted, NSError *error) {
                                      Q_UNUSED(granted)
                                      Q_UNUSED(error)
                                  }];
        }];
    }

    void Post(const QString &title, const QString &body,
              const std::function<void()> &onActivated,
              const std::function<void()> &onRefused) {
        UNUserNotificationCenter *center = notificationCenter();
        if (center == nil) {
            toUiThread(onRefused);
            return;
        }

        {
            std::lock_guard<std::mutex> guard(g_activatedLock);
            g_activated = onActivated;
        }

        const QString safeTitle = title;
        const QString safeBody = body;
        const std::function<void()> safeRefused = onRefused;

        [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings *settings) {
            switch (settings.authorizationStatus) {
                case UNAuthorizationStatusAuthorized:
                case UNAuthorizationStatusProvisional:
                    deliver(center, safeTitle, safeBody, safeRefused);
                    break;

                case UNAuthorizationStatusNotDetermined: {
                    // Первый раз спрашиваем разрешение. Человек может думать
                    // долго — карточку в это время не показываем, иначе он
                    // получит два уведомления об одном и том же.
                    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert |
                                                             UNAuthorizationOptionSound)
                                          completionHandler:^(BOOL granted, NSError *error) {
                                              Q_UNUSED(error)
                                              if (granted) {
                                                  deliver(center, safeTitle, safeBody, safeRefused);
                                              } else {
                                                  toUiThread(safeRefused);
                                              }
                                          }];
                    break;
                }

                default:
                    // Запрещено в настройках системы — показываем свою карточку.
                    toUiThread(safeRefused);
                    break;
            }
        }];
    }
} // namespace MacNotify
