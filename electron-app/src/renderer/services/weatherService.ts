/**
 * 天气服务模块
 * 支持和风天气API + 2小时缓存机制
 */

import { weatherConfig } from '../config/weatherConfig';
import { settingsService } from './settingsService';
import { weatherCacheService } from './weatherCacheService';

export interface WeatherData {
  temperature: number;
  feelsLike: number;
  humidity: number;
  condition: string;
  conditionCode: number;
  city: string;
  updateTime: string;
  windSpeed?: number;
  windDirection?: string;
  pressure?: number;
  visibility?: number;
}

export interface WeatherConfig {
  apiKey: string;
  city: string;
  lang?: 'zh' | 'en';
}

class WeatherService {
  private lang: string = weatherConfig.lang;
  // 如果没有配置 apiHost，尝试使用免费版端点
  private useMockData: boolean = weatherConfig.useMockData;

  // 动态获取 API Key（从设置中读取）
  private getApiKey(): string {
    const settings = settingsService.getWeatherSettings();
    return settings.apiKey || weatherConfig.apiKey;
  }

  // 动态获取 API Host
  private getApiHost(): string {
    return weatherConfig.apiHost;
  }

  /**
   * 配置天气服务
   */
  configure(config: WeatherConfig) {
    // this.apiKey = config.apiKey; // 已移除，使用动态获取
    this.lang = config.lang || 'zh';
  }

  /**
   * 获取城市ID（和风天气需要先查询城市ID）
   */
  async getCityId(cityName: string): Promise<string> {
    const apiKey = this.getApiKey();
    const apiHost = this.getApiHost();

    if (!apiKey) {
      throw new Error('API Key 未配置');
    }

    try {
      // 如果有自定义 API Host，使用自定义 Host + Header 认证
      // 否则尝试使用免费版端点 + URL 参数认证
      let url: string;
      let headers: HeadersInit = {};

      if (apiHost) {
        url = `https://${apiHost}/v7/city/lookup?location=${encodeURIComponent(cityName)}`;
        headers = { 'X-QW-Api-Key': apiKey };
      } else {
        // 尝试免费版端点（使用 key 参数）
        url = `https://geoapi.qweather.com/v2/city/lookup?location=${encodeURIComponent(cityName)}&key=${apiKey}`;
      }

      console.log('查询城市ID:', url);

      const response = await fetch(url, { headers });

      // 检查响应状态
      if (!response.ok) {
        console.error('API 响应错误:', response.status, response.statusText);
        throw new Error(`API 请求失败: ${response.status}`);
      }

      const text = await response.text();
      console.log('API 响应:', text);

      if (!text) {
        throw new Error('API 返回空响应');
      }

      const data = JSON.parse(text);

      if (data.code === '200' && data.location && data.location.length > 0) {
        return data.location[0].id;
      } else {
        console.error('城市查询失败，返回码:', data.code);
        throw new Error(`城市查询失败: ${data.code}`);
      }
    } catch (error) {
      console.error('获取城市ID失败:', error);
      throw error;
    }
  }

  /**
   * 获取指定城市的实时天气（带缓存）
   */
  async getWeatherByCity(cityName: string, cityId?: string): Promise<WeatherData> {
    // 先检查缓存
    const cached = weatherCacheService.getCachedWeather(cityName);
    if (cached) {
      return cached;
    }

    // 如果配置为使用模拟数据
    if (this.useMockData) {
      console.log('使用模拟天气数据');
      return this.getMockWeatherData(cityName);
    }

    const apiKey = this.getApiKey();
    const apiHost = this.getApiHost();

    if (!apiKey) {
      console.log('未配置 API Key，使用模拟数据');
      return this.getMockWeatherData(cityName);
    }

    try {
      // 如果没有提供 cityId，先查询
      let locationId = cityId;
      if (!locationId) {
        locationId = await this.getCityId(cityName);
      }

      let url: string;
      let headers: HeadersInit = {};

      if (apiHost) {
        url = `https://${apiHost}/v7/weather/now?location=${locationId}&lang=${this.lang}`;
        headers = { 'X-QW-Api-Key': apiKey };
      } else {
        // 尝试免费版端点
        url = `https://devapi.qweather.com/v7/weather/now?location=${locationId}&key=${apiKey}&lang=${this.lang}`;
      }

      console.log('获取天气:', url);

      const response = await fetch(url, { headers });

      if (!response.ok) {
        console.error('天气API响应错误:', response.status);
        throw new Error(`天气API请求失败: ${response.status}`);
      }

      const data = await response.json();
      console.log('天气数据:', data);

      if (data.code === '200' && data.now) {
        const now = data.now;
        const weatherData: WeatherData = {
          temperature: parseInt(now.temp),
          feelsLike: parseInt(now.feelsLike),
          humidity: parseInt(now.humidity),
          condition: now.text,
          conditionCode: parseInt(now.icon),
          city: cityName,
          updateTime: new Date(data.updateTime).toLocaleTimeString('zh-CN', {
            hour: '2-digit',
            minute: '2-digit',
          }),
          windSpeed: parseFloat(now.windSpeed),
          windDirection: now.windDir,
          pressure: parseInt(now.pressure),
          visibility: parseInt(now.vis),
        };

        // 保存到缓存
        weatherCacheService.setCachedWeather(cityName, locationId, weatherData);

        return weatherData;
      } else {
        throw new Error(`天气数据获取失败: ${data.code}`);
      }
    } catch (error) {
      console.error('获取天气数据失败:', error);
      // 失败时返回模拟数据
      return this.getMockWeatherData(cityName);
    }
  }

  /**
   * 获取当前选中城市的天气
   */
  async getCurrentWeather(): Promise<WeatherData> {
    const currentCity = settingsService.getCurrentCity();
    if (!currentCity) {
      throw new Error('未配置城市');
    }

    return this.getWeatherByCity(currentCity.name, currentCity.locationId);
  }

  /**
   * 预加载所有城市的天气数据
   * 在应用启动或进入天气页面时调用
   */
  async preloadAllCitiesWeather(): Promise<void> {
    const settings = settingsService.getWeatherSettings();
    const cities = settings.cities;

    console.log(`开始预加载 ${cities.length} 个城市的天气数据...`);

    // 并发请求所有城市的天气
    const promises = cities.map(city =>
      this.getWeatherByCity(city.name, city.locationId).catch(error => {
        console.error(`预加载城市 ${city.name} 失败:`, error);
        return null;
      })
    );

    const results = await Promise.all(promises);
    const successCount = results.filter(r => r !== null).length;

    console.log(`预加载完成: ${successCount}/${cities.length} 个城市成功`);
  }

  /**
   * 获取模拟天气数据（用于开发测试）
   */
  private getMockWeatherData(cityName: string): WeatherData {
    const conditions = [
      { code: 100, text: '晴天', temp: 22 },
      { code: 101, text: '多云', temp: 18 },
      { code: 104, text: '阴天', temp: 15 },
      { code: 300, text: '阵雨', temp: 16 },
      { code: 305, text: '小雨', temp: 14 },
      { code: 400, text: '小雪', temp: -2 },
    ];

    const randomCondition = conditions[Math.floor(Math.random() * conditions.length)];

    return {
      temperature: randomCondition.temp,
      feelsLike: randomCondition.temp - 2,
      humidity: 60 + Math.floor(Math.random() * 20),
      condition: randomCondition.text,
      conditionCode: randomCondition.code,
      city: cityName,
      updateTime: new Date().toLocaleTimeString('zh-CN', {
        hour: '2-digit',
        minute: '2-digit',
      }),
      windSpeed: 3.5,
      windDirection: '东南风',
      pressure: 1013,
      visibility: 10,
    };
  }
}

// 导出单例
export const weatherService = new WeatherService();
