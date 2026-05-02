package it.naamah.asi.cooler.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val DarkColors = darkColorScheme(
    primary = Color(0xFF7DD3FC),
    secondary = Color(0xFF93C5FD),
    tertiary = Color(0xFF6EE7B7),
    background = Color(0xFF08111F),
    surface = Color(0xFF111D32),
    surfaceVariant = Color(0xFF0F1A2D),
)

private val LightColors = lightColorScheme(
    primary = Color(0xFF0F4C81),
    secondary = Color(0xFF2563EB),
    tertiary = Color(0xFF047857),
)

@Composable
fun CoolerTheme(content: @Composable () -> Unit) {
  MaterialTheme(
      colorScheme = if (isSystemInDarkTheme()) DarkColors else LightColors,
      typography = androidx.compose.material3.Typography(),
      content = content)
}
