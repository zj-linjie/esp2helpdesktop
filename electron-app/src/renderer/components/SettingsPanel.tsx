import React, { useState, useEffect } from 'react';
import {
  Box,
  Typography,
  Card,
  CardContent,
  List,
  ListItem,
  ListItemText,
  IconButton,
  TextField,
  Button,
  Divider,
  Alert,
  Slider,
  Switch,
  FormControlLabel,
  Select,
  MenuItem,
  FormControl,
  InputLabel,
  Chip,
} from '@mui/material';
import { Delete, Add, Save, Visibility, VisibilityOff, FolderOpen, Palette, Apps, Refresh } from '@mui/icons-material';
import { settingsService, CityConfig } from '../services/settingsService';
import { weatherConfig } from '../config/weatherConfig';
import { photoThemes } from '../config/photoThemes';
import { appLauncherService, MacApp } from '../services/appLauncherService';

const SettingsPanel: React.FC = () => {
  const [cities, setCities] = useState<CityConfig[]>([]);
  const [newCityName, setNewCityName] = useState('');
  const [apiKey, setApiKey] = useState('');
  const [showApiKey, setShowApiKey] = useState(false);
  const [saveSuccess, setSaveSuccess] = useState(false);

  // Photo settings
  const [photoFolder, setPhotoFolder] = useState('');
  const [slideshowInterval, setSlideshowInterval] = useState(5);
  const [autoPlay, setAutoPlay] = useState(true);
  const [photoTheme, setPhotoTheme] = useState('dark-gallery');
  const [maxFileSize, setMaxFileSize] = useState(2);
  const [autoCompress, setAutoCompress] = useState(true);
  const [photoSaveSuccess, setPhotoSaveSuccess] = useState(false);

  // App Launcher settings
  const [apps, setApps] = useState<MacApp[]>([]);
  const [allApps, setAllApps] = useState<MacApp[]>([]);
  const [isScanning, setIsScanning] = useState(false);
  const [appSaveSuccess, setAppSaveSuccess] = useState(false);
  const [appSearchQuery, setAppSearchQuery] = useState('');

  // Load cities and API key
  useEffect(() => {
    loadCities();
    loadApiKey();
    loadPhotoSettings();
    loadApps().catch((error) => {
      console.error('加载应用设置失败:', error);
    });
  }, []);

  const loadCities = () => {
    const settings = settingsService.getWeatherSettings();
    setCities(settings.cities);
  };

  const loadApiKey = () => {
    const settings = settingsService.getWeatherSettings();
    setApiKey(settings.apiKey || weatherConfig.apiKey);
  };

  const loadPhotoSettings = () => {
    const settings = settingsService.getPhotoSettings();
    setPhotoFolder(settings.folderPath);
    setSlideshowInterval(settings.slideshowInterval);
    setAutoPlay(settings.autoPlay);
    setPhotoTheme(settings.theme);
    setMaxFileSize(settings.maxFileSize);
    setAutoCompress(settings.autoCompress);
  };

  const loadApps = async () => {
    const fromMain = await appLauncherService.loadSettingsFromMainProcess();
    if (fromMain) {
      setApps(fromMain);
      return;
    }
    const savedApps = appLauncherService.getApps();
    setApps(savedApps);
  };

  const handleScanApps = async () => {
    setIsScanning(true);
    try {
      const scannedApps = await appLauncherService.scanApplications();
      setAllApps(scannedApps);
    } catch (error) {
      console.error('扫描应用失败:', error);
    } finally {
      setIsScanning(false);
    }
  };

  const handleAddApp = (app: MacApp) => {
    appLauncherService.addApp(app);
    loadApps().catch((error) => console.error('刷新应用列表失败:', error));
    setAppSaveSuccess(true);
    setTimeout(() => setAppSaveSuccess(false), 2000);
  };

  const handleRemoveApp = (appId: string) => {
    appLauncherService.removeApp(appId);
    loadApps().catch((error) => console.error('刷新应用列表失败:', error));
  };

  // 过滤可添加的应用
  const getFilteredAvailableApps = () => {
    const availableApps = allApps.filter(app => !apps.find(a => a.path === app.path));

    if (!appSearchQuery.trim()) {
      return availableApps;
    }

    const query = appSearchQuery.toLowerCase();
    return availableApps.filter(app =>
      app.name.toLowerCase().includes(query) ||
      app.path.toLowerCase().includes(query)
    );
  };

  const handleAddCity = () => {
    if (newCityName.trim()) {
      settingsService.addCity(newCityName.trim());
      setNewCityName('');
      loadCities();
    }
  };

  const handleDeleteCity = (cityId: string) => {
    settingsService.removeCity(cityId);
    loadCities();
  };

  const handleSaveApiKey = () => {
    settingsService.updateApiKey(apiKey.trim());
    setSaveSuccess(true);
    setTimeout(() => setSaveSuccess(false), 3000);
  };

  const handleSavePhotoSettings = () => {
    settingsService.updatePhotoFolder(photoFolder.trim());
    settingsService.updateSlideshowInterval(slideshowInterval);
    settingsService.updateAutoPlay(autoPlay);
    settingsService.updatePhotoTheme(photoTheme);
    settingsService.updateMaxFileSize(maxFileSize);
    settingsService.updateAutoCompress(autoCompress);
    setPhotoSaveSuccess(true);
    setTimeout(() => setPhotoSaveSuccess(false), 3000);
  };

  return (
    <Box
      sx={{
        padding: 3,
        maxWidth: 800,
        margin: '0 auto',
      }}
    >
      <Typography variant="h5" sx={{ mb: 3, fontWeight: 600 }}>
        设置
      </Typography>

      {/* API Key Settings Card */}
      <Card
        sx={{
          backgroundColor: '#1e1e1e',
          borderRadius: 2,
          mb: 3,
        }}
      >
        <CardContent>
          <Typography variant="h6" sx={{ mb: 2, fontWeight: 600 }}>
            和风天气 API Key
          </Typography>

          <Typography
            variant="body2"
            sx={{ mb: 2, color: 'rgba(255, 255, 255, 0.7)' }}
          >
            配置你的和风天气 API Key，用于获取实时天气数据。
          </Typography>

          {saveSuccess && (
            <Alert severity="success" sx={{ mb: 2 }}>
              API Key 保存成功！刷新页面后生效。
            </Alert>
          )}

          <Box sx={{ display: 'flex', gap: 1, mb: 2 }}>
            <TextField
              fullWidth
              size="small"
              type={showApiKey ? 'text' : 'password'}
              placeholder="输入和风天气 API Key"
              value={apiKey}
              onChange={(e) => setApiKey(e.target.value)}
              sx={{
                '& .MuiOutlinedInput-root': {
                  backgroundColor: 'rgba(255, 255, 255, 0.05)',
                  fontFamily: 'monospace',
                  '& fieldset': {
                    borderColor: 'rgba(255, 255, 255, 0.2)',
                  },
                  '&:hover fieldset': {
                    borderColor: 'rgba(255, 255, 255, 0.3)',
                  },
                },
              }}
              InputProps={{
                endAdornment: (
                  <IconButton
                    size="small"
                    onClick={() => setShowApiKey(!showApiKey)}
                    sx={{ color: 'rgba(255, 255, 255, 0.5)' }}
                  >
                    {showApiKey ? <VisibilityOff /> : <Visibility />}
                  </IconButton>
                ),
              }}
            />
            <Button
              variant="contained"
              startIcon={<Save />}
              onClick={handleSaveApiKey}
              disabled={!apiKey.trim()}
              sx={{
                minWidth: '100px',
                backgroundColor: '#2e7d32',
                '&:hover': {
                  backgroundColor: '#1b5e20',
                },
              }}
            >
              保存
            </Button>
          </Box>

          <Typography
            variant="caption"
            sx={{ color: 'rgba(255, 255, 255, 0.5)', display: 'block' }}
          >
            获取 API Key：访问{' '}
            <a
              href="https://dev.qweather.com/"
              target="_blank"
              rel="noopener noreferrer"
              style={{ color: '#1976d2' }}
            >
              和风天气开发平台
            </a>{' '}
            注册并创建应用
          </Typography>
        </CardContent>
      </Card>

      {/* Weather Settings Card */}
      <Card
        sx={{
          backgroundColor: '#1e1e1e',
          borderRadius: 2,
          mb: 3,
        }}
      >
        <CardContent>
          <Typography variant="h6" sx={{ mb: 2, fontWeight: 600 }}>
            天气城市管理
          </Typography>

          <Typography
            variant="body2"
            sx={{ mb: 2, color: 'rgba(255, 255, 255, 0.7)' }}
          >
            在此添加或删除城市。在天气页面点击城市名可以切换显示的城市。
          </Typography>

          {/* Add City */}
          <Box sx={{ display: 'flex', gap: 1, mb: 3 }}>
            <TextField
              fullWidth
              size="small"
              placeholder="输入城市名称（如：北京、上海）"
              value={newCityName}
              onChange={(e) => setNewCityName(e.target.value)}
              onKeyPress={(e) => {
                if (e.key === 'Enter') {
                  handleAddCity();
                }
              }}
              sx={{
                '& .MuiOutlinedInput-root': {
                  backgroundColor: 'rgba(255, 255, 255, 0.05)',
                  '& fieldset': {
                    borderColor: 'rgba(255, 255, 255, 0.2)',
                  },
                  '&:hover fieldset': {
                    borderColor: 'rgba(255, 255, 255, 0.3)',
                  },
                },
              }}
            />
            <Button
              variant="contained"
              startIcon={<Add />}
              onClick={handleAddCity}
              sx={{
                minWidth: '100px',
                backgroundColor: '#1976d2',
                '&:hover': {
                  backgroundColor: '#1565c0',
                },
              }}
            >
              添加
            </Button>
          </Box>

          <Divider sx={{ mb: 2, borderColor: 'rgba(255, 255, 255, 0.1)' }} />

          {/* City List */}
          <Typography variant="subtitle2" sx={{ mb: 1, color: 'rgba(255, 255, 255, 0.7)' }}>
            已添加的城市 ({cities.length})
          </Typography>

          {cities.length === 0 ? (
            <Typography
              variant="body2"
              sx={{ color: 'rgba(255, 255, 255, 0.5)', textAlign: 'center', py: 3 }}
            >
              暂无城市，请添加
            </Typography>
          ) : (
            <List sx={{ py: 0 }}>
              {cities.map((city, index) => (
                <ListItem
                  key={city.id}
                  sx={{
                    backgroundColor: 'rgba(255, 255, 255, 0.03)',
                    borderRadius: 1,
                    mb: 1,
                    '&:hover': {
                      backgroundColor: 'rgba(255, 255, 255, 0.06)',
                    },
                  }}
                  secondaryAction={
                    <IconButton
                      edge="end"
                      onClick={() => handleDeleteCity(city.id)}
                      disabled={cities.length === 1}
                      sx={{
                        color: cities.length === 1 ? 'rgba(255, 255, 255, 0.3)' : '#f44336',
                        '&:hover': {
                          backgroundColor: 'rgba(244, 67, 54, 0.1)',
                        },
                      }}
                    >
                      <Delete />
                    </IconButton>
                  }
                >
                  <ListItemText
                    primary={city.name}
                    secondary={city.locationId ? `ID: ${city.locationId}` : '未查询'}
                    primaryTypographyProps={{
                      fontWeight: 500,
                    }}
                    secondaryTypographyProps={{
                      sx: { color: 'rgba(255, 255, 255, 0.5)', fontSize: '0.75rem' },
                    }}
                  />
                </ListItem>
              ))}
            </List>
          )}

          {cities.length === 1 && (
            <Typography
              variant="caption"
              sx={{ color: 'rgba(255, 255, 255, 0.5)', display: 'block', mt: 1 }}
            >
              * 至少保留一个城市
            </Typography>
          )}
        </CardContent>
      </Card>

      {/* Info Card */}
      <Card
        sx={{
          backgroundColor: 'rgba(33, 150, 243, 0.1)',
          borderRadius: 2,
          border: '1px solid rgba(33, 150, 243, 0.3)',
        }}
      >
        <CardContent>
          <Typography variant="subtitle2" sx={{ mb: 1, fontWeight: 600 }}>
            💡 提示
          </Typography>
          <Typography variant="body2" sx={{ color: 'rgba(255, 255, 255, 0.8)' }}>
            • 在天气页面点击城市名称可以快速切换城市
            <br />
            • 天气数据每30分钟自动更新一次
            <br />
            • 天气数据会缓存2小时，减少API调用
            <br />
            • 等硬件到货后，可以通过 ESP32 配网页面配置城市
          </Typography>
        </CardContent>
      </Card>

      {/* App Launcher Settings Card */}
      <Card
        sx={{
          backgroundColor: '#1e1e1e',
          borderRadius: 2,
          mt: 3,
        }}
      >
        <CardContent>
          <Typography variant="h6" sx={{ mb: 2, fontWeight: 600 }}>
            应用启动器管理
          </Typography>

          <Typography
            variant="body2"
            sx={{ mb: 2, color: 'rgba(255, 255, 255, 0.7)' }}
          >
            管理快捷启动的 macOS 应用，支持无限数量应用。
          </Typography>

          {appSaveSuccess && (
            <Alert severity="success" sx={{ mb: 2 }}>
              应用列表已更新！
            </Alert>
          )}

          {/* Scan Apps Button */}
          <Button
            variant="outlined"
            startIcon={isScanning ? null : <Refresh />}
            onClick={handleScanApps}
            disabled={isScanning}
            fullWidth
            sx={{
              mb: 3,
              borderColor: 'rgba(255, 255, 255, 0.2)',
              color: '#1976d2',
              '&:hover': {
                borderColor: '#1976d2',
                backgroundColor: 'rgba(25, 118, 210, 0.1)',
              },
            }}
          >
            {isScanning ? '扫描中...' : '扫描 /Applications 文件夹'}
          </Button>

          <Divider sx={{ mb: 2, borderColor: 'rgba(255, 255, 255, 0.1)' }} />

          {/* Current Apps */}
          <Typography variant="subtitle2" sx={{ mb: 1, color: 'rgba(255, 255, 255, 0.7)' }}>
            已添加的应用 ({apps.length})
          </Typography>

          {apps.length === 0 ? (
            <Typography
              variant="body2"
              sx={{ color: 'rgba(255, 255, 255, 0.5)', textAlign: 'center', py: 3 }}
            >
              暂无应用，请扫描并添加
            </Typography>
          ) : (
            <List sx={{ py: 0, mb: 3 }}>
              {apps.map((app) => (
                <ListItem
                  key={app.id}
                  sx={{
                    backgroundColor: 'rgba(255, 255, 255, 0.03)',
                    borderRadius: 1,
                    mb: 1,
                    '&:hover': {
                      backgroundColor: 'rgba(255, 255, 255, 0.06)',
                    },
                  }}
                  secondaryAction={
                    <IconButton
                      edge="end"
                      onClick={() => handleRemoveApp(app.id)}
                      sx={{
                        color: '#f44336',
                        '&:hover': {
                          backgroundColor: 'rgba(244, 67, 54, 0.1)',
                        },
                      }}
                    >
                      <Delete />
                    </IconButton>
                  }
                >
                  <Box
                    sx={{
                      width: '32px',
                      height: '32px',
                      borderRadius: '50%',
                      backgroundColor: app.icon?.startsWith('LETTER:')
                        ? app.icon.split(':')[2]
                        : '#2a2a2a',
                      display: 'flex',
                      alignItems: 'center',
                      justifyContent: 'center',
                      mr: 2,
                      flexShrink: 0,
                    }}
                  >
                    {app.icon?.startsWith('LETTER:') ? (
                      <span style={{
                        fontSize: '1rem',
                        fontWeight: 'bold',
                        color: '#ffffff',
                      }}>
                        {app.icon.split(':')[1]}
                      </span>
                    ) : (
                      <span style={{ fontSize: '1.2rem' }}>{app.icon || '📱'}</span>
                    )}
                  </Box>
                  <ListItemText
                    primary={app.name}
                    secondary={app.path}
                    primaryTypographyProps={{
                      fontWeight: 500,
                    }}
                    secondaryTypographyProps={{
                      sx: {
                        color: 'rgba(255, 255, 255, 0.5)',
                        fontSize: '0.7rem',
                        overflow: 'hidden',
                        textOverflow: 'ellipsis',
                        whiteSpace: 'nowrap',
                      },
                    }}
                  />
                </ListItem>
              ))}
            </List>
          )}

          {/* Available Apps */}
          {allApps.length > 0 && (
            <>
              <Divider sx={{ mb: 2, borderColor: 'rgba(255, 255, 255, 0.1)' }} />
              <Typography variant="subtitle2" sx={{ mb: 1, color: 'rgba(255, 255, 255, 0.7)' }}>
                可添加的应用 ({getFilteredAvailableApps().length})
              </Typography>

              {/* Search Box */}
              <TextField
                fullWidth
                size="small"
                placeholder="搜索应用名称..."
                value={appSearchQuery}
                onChange={(e) => setAppSearchQuery(e.target.value)}
                sx={{
                  mb: 2,
                  '& .MuiOutlinedInput-root': {
                    backgroundColor: 'rgba(255, 255, 255, 0.05)',
                    '& fieldset': {
                      borderColor: 'rgba(255, 255, 255, 0.2)',
                    },
                    '&:hover fieldset': {
                      borderColor: 'rgba(255, 255, 255, 0.3)',
                    },
                  },
                }}
                InputProps={{
                  startAdornment: (
                    <Apps sx={{ mr: 1, color: 'rgba(255, 255, 255, 0.5)', fontSize: '1.2rem' }} />
                  ),
                }}
              />

              <List sx={{ py: 0, maxHeight: '300px', overflow: 'auto' }}>
                {getFilteredAvailableApps().length === 0 ? (
                  <Typography
                    variant="body2"
                    sx={{ color: 'rgba(255, 255, 255, 0.5)', textAlign: 'center', py: 3 }}
                  >
                    {appSearchQuery ? '未找到匹配的应用' : '所有应用已添加'}
                  </Typography>
                ) : (
                  getFilteredAvailableApps().map((app) => (
                    <ListItem
                      key={app.id}
                      sx={{
                        backgroundColor: 'rgba(255, 255, 255, 0.03)',
                        borderRadius: 1,
                        mb: 1,
                        '&:hover': {
                          backgroundColor: 'rgba(255, 255, 255, 0.06)',
                        },
                      }}
                      secondaryAction={
                        <IconButton
                          edge="end"
                          onClick={() => handleAddApp(app)}
                          sx={{
                            color: '#4caf50',
                            '&:hover': {
                              backgroundColor: 'rgba(76, 175, 80, 0.1)',
                            },
                          }}
                        >
                          <Add />
                        </IconButton>
                      }
                    >
                      <Box
                        sx={{
                          width: '32px',
                          height: '32px',
                          borderRadius: '50%',
                          backgroundColor: app.icon?.startsWith('LETTER:')
                            ? app.icon.split(':')[2]
                            : '#2a2a2a',
                          display: 'flex',
                          alignItems: 'center',
                          justifyContent: 'center',
                          mr: 2,
                          flexShrink: 0,
                        }}
                      >
                        {app.icon?.startsWith('LETTER:') ? (
                          <span style={{
                            fontSize: '1rem',
                            fontWeight: 'bold',
                            color: '#ffffff',
                          }}>
                            {app.icon.split(':')[1]}
                          </span>
                        ) : (
                          <span style={{ fontSize: '1.2rem' }}>{app.icon || '📱'}</span>
                        )}
                      </Box>
                      <ListItemText
                        primary={app.name}
                        primaryTypographyProps={{
                          fontWeight: 500,
                        }}
                      />
                    </ListItem>
                  ))
                )}
              </List>
            </>
          )}
        </CardContent>
      </Card>

      {/* Photo Frame Settings Card */}
      <Card
        sx={{
          backgroundColor: '#1e1e1e',
          borderRadius: 2,
          mt: 3,
        }}
      >
        <CardContent>
          <Typography variant="h6" sx={{ mb: 2, fontWeight: 600 }}>
            电子相框设置
          </Typography>

          <Typography
            variant="body2"
            sx={{ mb: 2, color: 'rgba(255, 255, 255, 0.7)' }}
          >
            配置相册文件夹路径和幻灯片播放设置。最多支持 20 张照片。
          </Typography>

          {photoSaveSuccess && (
            <Alert severity="success" sx={{ mb: 2 }}>
              相册设置保存成功！
            </Alert>
          )}

          {/* Folder Path */}
          <Box sx={{ mb: 3 }}>
            <Typography variant="subtitle2" sx={{ mb: 1, color: 'rgba(255, 255, 255, 0.9)' }}>
              相册文件夹路径
            </Typography>
            <Box sx={{ display: 'flex', gap: 1 }}>
              <TextField
                fullWidth
                size="small"
                placeholder="/photos 或 /path/to/your/photos"
                value={photoFolder}
                onChange={(e) => setPhotoFolder(e.target.value)}
                sx={{
                  '& .MuiOutlinedInput-root': {
                    backgroundColor: 'rgba(255, 255, 255, 0.05)',
                    fontFamily: 'monospace',
                    '& fieldset': {
                      borderColor: 'rgba(255, 255, 255, 0.2)',
                    },
                    '&:hover fieldset': {
                      borderColor: 'rgba(255, 255, 255, 0.3)',
                    },
                  },
                }}
                InputProps={{
                  startAdornment: (
                    <FolderOpen sx={{ mr: 1, color: 'rgba(255, 255, 255, 0.5)', fontSize: '1.2rem' }} />
                  ),
                }}
              />
            </Box>
            <Typography
              variant="caption"
              sx={{ color: 'rgba(255, 255, 255, 0.5)', display: 'block', mt: 0.5 }}
            >
              目前使用模拟数据，硬件到货后支持从 TF/SD 卡加载照片
            </Typography>
          </Box>

          <Divider sx={{ mb: 3, borderColor: 'rgba(255, 255, 255, 0.1)' }} />

          {/* Theme Selection */}
          <Box sx={{ mb: 3 }}>
            <Typography variant="subtitle2" sx={{ mb: 1, color: 'rgba(255, 255, 255, 0.9)' }}>
              视觉主题
            </Typography>
            <FormControl fullWidth size="small">
              <Select
                value={photoTheme}
                onChange={(e) => setPhotoTheme(e.target.value)}
                sx={{
                  backgroundColor: 'rgba(255, 255, 255, 0.05)',
                  '& .MuiOutlinedInput-notchedOutline': {
                    borderColor: 'rgba(255, 255, 255, 0.2)',
                  },
                  '&:hover .MuiOutlinedInput-notchedOutline': {
                    borderColor: 'rgba(255, 255, 255, 0.3)',
                  },
                  '& .MuiSvgIcon-root': {
                    color: 'rgba(255, 255, 255, 0.7)',
                  },
                }}
                startAdornment={
                  <Palette sx={{ mr: 1, color: 'rgba(255, 255, 255, 0.5)', fontSize: '1.2rem' }} />
                }
              >
                {photoThemes.map((theme) => (
                  <MenuItem key={theme.name} value={theme.name}>
                    <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                      <Typography>{theme.displayName}</Typography>
                      {theme.name === 'dark-gallery' && (
                        <Chip label="奢华" size="small" sx={{ height: '20px', fontSize: '0.7rem' }} />
                      )}
                      {theme.name === 'light-gallery' && (
                        <Chip label="清新" size="small" sx={{ height: '20px', fontSize: '0.7rem' }} />
                      )}
                      {theme.name === 'adaptive' && (
                        <Chip label="智能" size="small" color="primary" sx={{ height: '20px', fontSize: '0.7rem' }} />
                      )}
                    </Box>
                  </MenuItem>
                ))}
              </Select>
            </FormControl>
            <Typography
              variant="caption"
              sx={{ color: 'rgba(255, 255, 255, 0.5)', display: 'block', mt: 0.5 }}
            >
              {photoTheme === 'dark-gallery' && '深色背景 + 金色点缀，博物馆级展示效果'}
              {photoTheme === 'light-gallery' && '浅色背景 + 清爽蓝，北欧极简风格'}
              {photoTheme === 'adaptive' && '根据照片主色调自动调整背景和文字颜色'}
            </Typography>
          </Box>

          <Divider sx={{ mb: 3, borderColor: 'rgba(255, 255, 255, 0.1)' }} />

          {/* File Size Limit */}
          <Box sx={{ mb: 3 }}>
            <Typography variant="subtitle2" sx={{ mb: 1, color: 'rgba(255, 255, 255, 0.9)' }}>
              文件大小限制: {maxFileSize} MB
            </Typography>
            <Slider
              value={maxFileSize}
              onChange={(_, value) => setMaxFileSize(value as number)}
              min={1}
              max={5}
              step={0.5}
              marks={[
                { value: 1, label: '1MB' },
                { value: 2, label: '2MB' },
                { value: 3, label: '3MB' },
                { value: 5, label: '5MB' },
              ]}
              sx={{
                color: '#f57c00',
                '& .MuiSlider-markLabel': {
                  color: 'rgba(255, 255, 255, 0.5)',
                  fontSize: '0.7rem',
                },
              }}
            />
            <Typography
              variant="caption"
              sx={{ color: 'rgba(255, 255, 255, 0.5)', display: 'block' }}
            >
              超过此大小的照片将{autoCompress ? '自动压缩' : '无法上传'}
            </Typography>
          </Box>

          {/* Auto Compress */}
          <Box sx={{ mb: 3 }}>
            <FormControlLabel
              control={
                <Switch
                  checked={autoCompress}
                  onChange={(e) => setAutoCompress(e.target.checked)}
                  sx={{
                    '& .MuiSwitch-switchBase.Mui-checked': {
                      color: '#f57c00',
                    },
                    '& .MuiSwitch-switchBase.Mui-checked + .MuiSwitch-track': {
                      backgroundColor: '#f57c00',
                    },
                  }}
                />
              }
              label={
                <Typography variant="body2" sx={{ color: 'rgba(255, 255, 255, 0.9)' }}>
                  自动压缩超大照片
                </Typography>
              }
            />
            <Typography
              variant="caption"
              sx={{ color: 'rgba(255, 255, 255, 0.5)', display: 'block', ml: 4 }}
            >
              开启后会自动将超过限制的照片压缩到指定大小
            </Typography>
          </Box>

          <Divider sx={{ mb: 3, borderColor: 'rgba(255, 255, 255, 0.1)' }} />

          {/* Slideshow Interval */}
          <Box sx={{ mb: 3 }}>
            <Typography variant="subtitle2" sx={{ mb: 1, color: 'rgba(255, 255, 255, 0.9)' }}>
              幻灯片切换间隔: {slideshowInterval} 秒
            </Typography>
            <Slider
              value={slideshowInterval}
              onChange={(_, value) => setSlideshowInterval(value as number)}
              min={3}
              max={30}
              step={1}
              marks={[
                { value: 3, label: '3s' },
                { value: 10, label: '10s' },
                { value: 20, label: '20s' },
                { value: 30, label: '30s' },
              ]}
              sx={{
                color: '#1976d2',
                '& .MuiSlider-markLabel': {
                  color: 'rgba(255, 255, 255, 0.5)',
                  fontSize: '0.7rem',
                },
              }}
            />
          </Box>

          {/* Auto Play */}
          <Box sx={{ mb: 3 }}>
            <FormControlLabel
              control={
                <Switch
                  checked={autoPlay}
                  onChange={(e) => setAutoPlay(e.target.checked)}
                  sx={{
                    '& .MuiSwitch-switchBase.Mui-checked': {
                      color: '#1976d2',
                    },
                    '& .MuiSwitch-switchBase.Mui-checked + .MuiSwitch-track': {
                      backgroundColor: '#1976d2',
                    },
                  }}
                />
              }
              label={
                <Typography variant="body2" sx={{ color: 'rgba(255, 255, 255, 0.9)' }}>
                  自动播放幻灯片
                </Typography>
              }
            />
            <Typography
              variant="caption"
              sx={{ color: 'rgba(255, 255, 255, 0.5)', display: 'block', ml: 4 }}
            >
              开启后进入相框页面会自动播放幻灯片
            </Typography>
          </Box>

          {/* Save Button */}
          <Button
            variant="contained"
            startIcon={<Save />}
            onClick={handleSavePhotoSettings}
            fullWidth
            sx={{
              backgroundColor: '#2e7d32',
              '&:hover': {
                backgroundColor: '#1b5e20',
              },
            }}
          >
            保存相册设置
          </Button>
        </CardContent>
      </Card>
    </Box>
  );
};

export default SettingsPanel;
