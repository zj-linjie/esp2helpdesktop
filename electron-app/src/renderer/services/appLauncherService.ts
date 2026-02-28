/**
 * macOS 应用管理服务
 * 扫描、配置和启动 macOS 应用
 */

export interface MacApp {
  id: string;
  name: string;
  path: string;
  icon?: string; // base64 图标
  bundleId?: string;
}

export interface AppLauncherSettings {
  apps: MacApp[];
  maxApps: number; // 最多显示的应用数量
}

const STORAGE_KEY = 'app_launcher_settings';

class AppLauncherService {
  private syncSettingsToMainProcess(settings: AppLauncherSettings): void {
    try {
      const { ipcRenderer } = window.require('electron');
      ipcRenderer.invoke('app-launcher-sync-settings', {
        apps: settings.apps,
      }).catch((error: unknown) => {
        console.error('同步应用启动器设置到主进程失败:', error);
      });
    } catch (error) {
      // In web-only environment, IPC may not be available.
      console.debug('IPC 不可用，跳过主进程同步');
    }
  }

  async loadSettingsFromMainProcess(): Promise<MacApp[] | null> {
    try {
      const { ipcRenderer } = window.require('electron');
      const result = await ipcRenderer.invoke('app-launcher-get-settings');
      if (result?.success && Array.isArray(result.apps)) {
        const settings = this.getSettings();
        settings.apps = result.apps;
        this.saveSettings(settings);
        return settings.apps;
      }
    } catch (error) {
      console.error('从主进程读取应用启动器设置失败:', error);
    }
    return null;
  }

  /**
   * 获取应用启动器设置
   */
  getSettings(): AppLauncherSettings {
    try {
      const stored = localStorage.getItem(STORAGE_KEY);
      if (stored) {
        const parsed = JSON.parse(stored);
        this.syncSettingsToMainProcess(parsed);
        return parsed;
      }
    } catch (error) {
      console.error('读取应用启动器设置失败:', error);
    }

    const defaultSettings = this.getDefaultSettings();
    this.syncSettingsToMainProcess(defaultSettings);
    return defaultSettings;
  }

  /**
   * 保存应用启动器设置
   */
  saveSettings(settings: AppLauncherSettings): void {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(settings));
      this.syncSettingsToMainProcess(settings);
    } catch (error) {
      console.error('保存应用启动器设置失败:', error);
    }
  }

  /**
   * 获取默认设置
   */
  private getDefaultSettings(): AppLauncherSettings {
    return {
      apps: [],
      maxApps: 999, // 移除限制，支持所有应用
    };
  }

  /**
   * 添加应用
   */
  addApp(app: MacApp): void {
    const settings = this.getSettings();

    // 检查是否已存在
    const exists = settings.apps.find(a => a.path === app.path);
    if (exists) {
      console.warn('应用已存在:', app.name);
      return;
    }

    settings.apps.push(app);
    this.saveSettings(settings);
  }

  /**
   * 删除应用
   */
  removeApp(appId: string): void {
    const settings = this.getSettings();
    settings.apps = settings.apps.filter(app => app.id !== appId);
    this.saveSettings(settings);
  }

  /**
   * 更新应用
   */
  updateApp(appId: string, updates: Partial<MacApp>): void {
    const settings = this.getSettings();
    const app = settings.apps.find(a => a.id === appId);
    if (app) {
      Object.assign(app, updates);
      this.saveSettings(settings);
    }
  }

  /**
   * 获取所有应用
   */
  getApps(): MacApp[] {
    return this.getSettings().apps;
  }

  /**
   * 根据字符串生成颜色（基于哈希）
   */
  private getColorFromString(str: string): string {
    let hash = 0;
    for (let i = 0; i < str.length; i++) {
      hash = str.charCodeAt(i) + ((hash << 5) - hash);
    }

    // 预定义的柔和色彩方案
    const colors = [
      '#FF6B6B', // 红色
      '#4ECDC4', // 青色
      '#45B7D1', // 蓝色
      '#FFA07A', // 橙色
      '#98D8C8', // 薄荷绿
      '#F7DC6F', // 黄色
      '#BB8FCE', // 紫色
      '#85C1E2', // 天蓝
      '#F8B739', // 金色
      '#52B788', // 绿色
      '#E76F51', // 珊瑚色
      '#2A9D8F', // 青绿
    ];

    return colors[Math.abs(hash) % colors.length];
  }

  /**
   * 根据应用名称获取对应的 emoji 图标或字母图标
   */
  private getEmojiIcon(appName: string): string {
    const name = appName.toLowerCase();

    // 常见应用的 emoji 映射
    const iconMap: { [key: string]: string } = {
      // 浏览器
      'safari': '🧭',
      'chrome': '🔵',
      'google chrome': '🔵',
      'firefox': '🦊',
      'edge': '🌊',
      'brave': '🦁',

      // 开发工具
      'visual studio code': '💻',
      'vscode': '💻',
      'xcode': '🔨',
      'terminal': '⚡',
      'iterm': '⚡',
      'docker': '🐳',
      'postman': '📮',

      // 通讯
      'mail': '✉️',
      'messages': '💬',
      'slack': '💼',
      'discord': '🎮',
      'zoom': '📹',
      'teams': '👥',
      'wechat': '💚',
      'qq': '🐧',

      // 办公
      'word': '📝',
      'excel': '📊',
      'powerpoint': '📽️',
      'keynote': '🎬',
      'pages': '📄',
      'numbers': '🔢',
      'notion': '📓',
      'obsidian': '🔮',

      // 系统工具
      'finder': '📁',
      'settings': '⚙️',
      'system preferences': '⚙️',
      'activity monitor': '📊',
      'calculator': '🧮',
      'calendar': '📅',
      'notes': '📝',
      'reminders': '✅',
      'photos': '📷',
      'preview': '👁️',

      // 媒体
      'music': '🎵',
      'spotify': '🎧',
      'itunes': '🎵',
      'vlc': '🎬',
      'quicktime': '▶️',
      'photoshop': '🎨',
      'illustrator': '✏️',
      'figma': '🎨',
      'sketch': '💎',

      // 其他
      'app store': '🛍️',
      'github': '🐙',
      'anaconda': '🐍',
      'python': '🐍',
      'java': '☕',
      'node': '🟢',
      'chatgpt': '🤖',
      'claude': '🤖',
    };

    // 精确匹配
    if (iconMap[name]) {
      return iconMap[name];
    }

    // 模糊匹配
    for (const [key, icon] of Object.entries(iconMap)) {
      if (name.includes(key) || key.includes(name)) {
        return icon;
      }
    }

    // 根据应用类型推测
    if (name.includes('player') || name.includes('video')) return '▶️';
    if (name.includes('music') || name.includes('audio')) return '🎵';
    if (name.includes('photo') || name.includes('image')) return '📷';
    if (name.includes('game')) return '🎮';
    if (name.includes('chat') || name.includes('message')) return '💬';
    if (name.includes('mail') || name.includes('email')) return '✉️';
    if (name.includes('browser')) return '🌐';
    if (name.includes('editor') || name.includes('code')) return '💻';
    if (name.includes('design')) return '🎨';
    if (name.includes('tool')) return '🔧';

    // 未匹配：返回字母图标标记
    return `LETTER:${appName.charAt(0).toUpperCase()}:${this.getColorFromString(appName)}`;
  }

  /**
   * 扫描 /Applications 文件夹
   * 注意：需要通过 Electron IPC 调用主进程
   */
  async scanApplications(): Promise<MacApp[]> {
    try {
      // 通过 IPC 调用主进程扫描
      const { ipcRenderer } = window.require('electron');
      const result = await ipcRenderer.invoke('scan-applications');

      if (result.success) {
        console.log(`扫描到 ${result.apps.length} 个应用`);

        // 使用 emoji 图标（加载所有应用，不限制数量）
        const appsWithIcons = result.apps.map((app: MacApp) => ({
          ...app,
          icon: this.getEmojiIcon(app.name),
        }));

        return appsWithIcons;
      } else {
        console.error('扫描应用失败:', result.error);
        return this.getMockApps();
      }
    } catch (error) {
      console.error('扫描应用失败:', error);
      return this.getMockApps();
    }
  }

  /**
   * 启动应用
   * 注意：需要通过 Electron IPC 调用主进程
   */
  async launchApp(appPath: string): Promise<boolean> {
    try {
      // 通过 IPC 调用主进程启动应用
      const { ipcRenderer } = window.require('electron');
      const result = await ipcRenderer.invoke('launch-app', appPath);

      if (result.success) {
        console.log('应用启动成功:', appPath);
        return true;
      } else {
        console.error('应用启动失败:', result.error);
        return false;
      }
    } catch (error) {
      console.error('启动应用失败:', error);
      return false;
    }
  }

  /**
   * 获取模拟应用数据（用于开发测试）
   */
  private getMockApps(): MacApp[] {
    return [
      {
        id: '1',
        name: 'Safari',
        path: '/Applications/Safari.app',
        icon: '🌐',
      },
      {
        id: '2',
        name: 'Chrome',
        path: '/Applications/Google Chrome.app',
        icon: '🔵',
      },
      {
        id: '3',
        name: 'VS Code',
        path: '/Applications/Visual Studio Code.app',
        icon: '💻',
      },
      {
        id: '4',
        name: 'Finder',
        path: '/System/Library/CoreServices/Finder.app',
        icon: '📁',
      },
      {
        id: '5',
        name: 'Mail',
        path: '/Applications/Mail.app',
        icon: '✉️',
      },
      {
        id: '6',
        name: 'Calendar',
        path: '/Applications/Calendar.app',
        icon: '📅',
      },
      {
        id: '7',
        name: 'Notes',
        path: '/Applications/Notes.app',
        icon: '📝',
      },
      {
        id: '8',
        name: 'Music',
        path: '/Applications/Music.app',
        icon: '🎵',
      },
    ];
  }
}

// 导出单例
export const appLauncherService = new AppLauncherService();
