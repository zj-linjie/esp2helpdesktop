/**
 * 城市配置数据结构和存储服务
 */

export interface CityConfig {
  id: string;
  name: string;
  locationId?: string; // 和风天气的城市ID（查询后缓存）
}

export interface WeatherSettings {
  cities: CityConfig[];
  currentCityId: string;
  apiKey: string;
}

export interface PhotoSettings {
  folderPath: string;
  slideshowInterval: number; // 秒
  autoPlay: boolean;
  theme: string; // 主题名称
  maxFileSize: number; // MB
  autoCompress: boolean; // 自动压缩
  maxPhotoCount: number; // 最大照片数量
}

export interface AppSettings {
  weather: WeatherSettings;
  photo: PhotoSettings;
}

const STORAGE_KEY = 'weather_settings';
const PHOTO_STORAGE_KEY = 'photo_settings';

class SettingsService {
  private syncPhotoSettingsToMainProcess(settings: PhotoSettings): void {
    try {
      const { ipcRenderer } = window.require('electron');
      ipcRenderer.invoke('photo-frame-sync-settings', {
        settings,
      }).catch((error: unknown) => {
        console.error('同步相册设置到主进程失败:', error);
      });
    } catch {
      // In browser-only mode, IPC is unavailable.
    }
  }

  async loadPhotoSettingsFromMainProcess(): Promise<PhotoSettings | null> {
    try {
      const { ipcRenderer } = window.require('electron');
      const result = await ipcRenderer.invoke('photo-frame-get-settings');
      if (result?.success && result.settings) {
        const normalized = result.settings as PhotoSettings;
        localStorage.setItem(PHOTO_STORAGE_KEY, JSON.stringify(normalized));
        return normalized;
      }
    } catch (error) {
      console.error('从主进程读取相册设置失败:', error);
    }
    return null;
  }

  /**
   * 获取天气设置
   */
  getWeatherSettings(): WeatherSettings {
    try {
      const stored = localStorage.getItem(STORAGE_KEY);
      if (stored) {
        return JSON.parse(stored);
      }
    } catch (error) {
      console.error('读取设置失败:', error);
    }

    // 返回默认设置
    return this.getDefaultSettings();
  }

  /**
   * 保存天气设置
   */
  saveWeatherSettings(settings: WeatherSettings): void {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(settings));
    } catch (error) {
      console.error('保存设置失败:', error);
    }
  }

  /**
   * 获取默认设置
   */
  private getDefaultSettings(): WeatherSettings {
    return {
      cities: [
        { id: '1', name: '北京' },
        { id: '2', name: '上海' },
        { id: '3', name: '广州' },
      ],
      currentCityId: '1',
      apiKey: '598a41cf8b404383a148d15a41fa0b55', // 默认 API Key
    };
  }

  /**
   * 添加城市
   */
  addCity(cityName: string): WeatherSettings {
    const settings = this.getWeatherSettings();
    const newCity: CityConfig = {
      id: Date.now().toString(),
      name: cityName,
    };
    settings.cities.push(newCity);
    this.saveWeatherSettings(settings);
    return settings;
  }

  /**
   * 删除城市
   */
  removeCity(cityId: string): WeatherSettings {
    const settings = this.getWeatherSettings();
    settings.cities = settings.cities.filter(city => city.id !== cityId);

    // 如果删除的是当前城市，切换到第一个城市
    if (settings.currentCityId === cityId && settings.cities.length > 0) {
      settings.currentCityId = settings.cities[0].id;
    }

    this.saveWeatherSettings(settings);
    return settings;
  }

  /**
   * 设置当前城市
   */
  setCurrentCity(cityId: string): WeatherSettings {
    const settings = this.getWeatherSettings();
    settings.currentCityId = cityId;
    this.saveWeatherSettings(settings);
    return settings;
  }

  /**
   * 获取当前城市
   */
  getCurrentCity(): CityConfig | null {
    const settings = this.getWeatherSettings();
    return settings.cities.find(city => city.id === settings.currentCityId) || null;
  }

  /**
   * 更新城市的 locationId（和风天气城市ID）
   */
  updateCityLocationId(cityId: string, locationId: string): void {
    const settings = this.getWeatherSettings();
    const city = settings.cities.find(c => c.id === cityId);
    if (city) {
      city.locationId = locationId;
      this.saveWeatherSettings(settings);
    }
  }

  /**
   * 更新 API Key
   */
  updateApiKey(apiKey: string): void {
    const settings = this.getWeatherSettings();
    settings.apiKey = apiKey;
    this.saveWeatherSettings(settings);
  }

  /**
   * 从 ESP32 接收配置（WebSocket）
   */
  syncFromESP32(config: Partial<WeatherSettings>): void {
    const settings = this.getWeatherSettings();

    if (config.cities) {
      settings.cities = config.cities;
    }
    if (config.currentCityId) {
      settings.currentCityId = config.currentCityId;
    }
    if (config.apiKey) {
      settings.apiKey = config.apiKey;
    }

    this.saveWeatherSettings(settings);
  }

  /**
   * 获取相册设置
   */
  getPhotoSettings(): PhotoSettings {
    try {
      const stored = localStorage.getItem(PHOTO_STORAGE_KEY);
      if (stored) {
        return JSON.parse(stored);
      }
    } catch (error) {
      console.error('读取相册设置失败:', error);
    }

    // 返回默认设置
    return this.getDefaultPhotoSettings();
  }

  /**
   * 保存相册设置
   */
  savePhotoSettings(settings: PhotoSettings): void {
    try {
      localStorage.setItem(PHOTO_STORAGE_KEY, JSON.stringify(settings));
      this.syncPhotoSettingsToMainProcess(settings);
    } catch (error) {
      console.error('保存相册设置失败:', error);
    }
  }

  /**
   * 获取默认相册设置
   */
  private getDefaultPhotoSettings(): PhotoSettings {
    return {
      folderPath: '/photos', // 默认相册文件夹
      slideshowInterval: 5, // 5秒切换
      autoPlay: true,
      theme: 'dark-gallery', // 默认主题：暗夜美术馆
      maxFileSize: 2, // 2MB
      autoCompress: true, // 自动压缩
      maxPhotoCount: 20, // 最多20张照片
    };
  }

  /**
   * 更新相册文件夹路径
   */
  updatePhotoFolder(folderPath: string): void {
    const settings = this.getPhotoSettings();
    settings.folderPath = folderPath;
    this.savePhotoSettings(settings);
  }

  /**
   * 更新幻灯片间隔
   */
  updateSlideshowInterval(interval: number): void {
    const settings = this.getPhotoSettings();
    settings.slideshowInterval = interval;
    this.savePhotoSettings(settings);
  }

  /**
   * 更新自动播放设置
   */
  updateAutoPlay(autoPlay: boolean): void {
    const settings = this.getPhotoSettings();
    settings.autoPlay = autoPlay;
    this.savePhotoSettings(settings);
  }

  /**
   * 更新相册主题
   */
  updatePhotoTheme(theme: string): void {
    const settings = this.getPhotoSettings();
    settings.theme = theme;
    this.savePhotoSettings(settings);
  }

  /**
   * 更新文件大小限制
   */
  updateMaxFileSize(maxFileSize: number): void {
    const settings = this.getPhotoSettings();
    settings.maxFileSize = maxFileSize;
    this.savePhotoSettings(settings);
  }

  /**
   * 更新自动压缩设置
   */
  updateAutoCompress(autoCompress: boolean): void {
    const settings = this.getPhotoSettings();
    settings.autoCompress = autoCompress;
    this.savePhotoSettings(settings);
  }
}

// 导出单例
export const settingsService = new SettingsService();
