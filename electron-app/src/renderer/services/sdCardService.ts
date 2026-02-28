export type SdFileType = 'image' | 'audio' | 'video' | 'other'

export interface SdFileItem {
  name: string
  path: string
  relativePath: string
  extension: string
  type: SdFileType
  size: number
  modifiedAt: number
}

export interface SdUploadProgressEvent {
  status: 'start' | 'progress' | 'file_complete' | 'file_error' | 'done' | 'canceled'
  fileIndex?: number
  fileCount?: number
  fileName?: string
  targetPath?: string
  fileBytesSent?: number
  fileTotalBytes?: number
  filePercent?: number
  overallBytesSent?: number
  overallTotalBytes?: number
  overallPercent?: number
  totalFiles?: number
  totalBytes?: number
  uploadedCount?: number
  skippedCount?: number
  reason?: string
  timestamp?: number
}

interface SdListResponse {
  success: boolean
  rootPath: string
  exists?: boolean
  truncated?: boolean
  error?: string
  files?: SdFileItem[]
  counts?: Record<SdFileType, number>
}

interface SdUploadResponse {
  success: boolean
  rootPath: string
  canceled?: boolean
  uploaded?: string[]
  renamed?: Array<{ sourceName: string; targetName: string; targetPath: string }>
  renamedCount?: number
  uploadedCount?: number
  skippedCount?: number
  error?: string
}

interface SdDeleteResponse {
  success: boolean
  rootPath: string
  deleted?: string
  error?: string
}

interface SdPreviewResponse {
  success: boolean
  rootPath: string
  filePath?: string
  previewDataUrl?: string
  mime?: string
  bytes?: number
  error?: string
}

const getIpcRenderer = () => {
  const electron = (window as any).require?.('electron')
  return electron?.ipcRenderer
}

class SdCardService {
  async listFiles(rootPath: string): Promise<SdListResponse> {
    try {
      const ipcRenderer = getIpcRenderer()
      if (!ipcRenderer) {
        return { success: false, rootPath, error: 'IPC unavailable' }
      }
      return await ipcRenderer.invoke('sd-manager-list-files', { rootPath }) as SdListResponse
    } catch (error) {
      return { success: false, rootPath, error: String(error) }
    }
  }

  async uploadFiles(rootPath: string): Promise<SdUploadResponse> {
    try {
      const ipcRenderer = getIpcRenderer()
      if (!ipcRenderer) {
        return { success: false, rootPath, error: 'IPC unavailable' }
      }
      return await ipcRenderer.invoke('sd-manager-upload-files', { rootPath }) as SdUploadResponse
    } catch (error) {
      return { success: false, rootPath, error: String(error) }
    }
  }

  async deleteFile(rootPath: string, filePath: string): Promise<SdDeleteResponse> {
    try {
      const ipcRenderer = getIpcRenderer()
      if (!ipcRenderer) {
        return { success: false, rootPath, error: 'IPC unavailable' }
      }
      return await ipcRenderer.invoke('sd-manager-delete-file', { rootPath, filePath }) as SdDeleteResponse
    } catch (error) {
      return { success: false, rootPath, error: String(error) }
    }
  }

  async previewFile(rootPath: string, filePath: string): Promise<SdPreviewResponse> {
    try {
      const ipcRenderer = getIpcRenderer()
      if (!ipcRenderer) {
        return { success: false, rootPath, error: 'IPC unavailable' }
      }
      return await ipcRenderer.invoke('sd-manager-preview-file', { rootPath, filePath }) as SdPreviewResponse
    } catch (error) {
      return { success: false, rootPath, error: String(error) }
    }
  }

  onUploadProgress(handler: (progress: SdUploadProgressEvent) => void): () => void {
    const ipcRenderer = getIpcRenderer()
    if (!ipcRenderer) {
      return () => {}
    }

    const listener = (_event: unknown, payload: SdUploadProgressEvent) => {
      handler(payload)
    }
    ipcRenderer.on('sd-upload-progress', listener)

    return () => {
      ipcRenderer.removeListener('sd-upload-progress', listener)
    }
  }
}

export const sdCardService = new SdCardService()
