package it.naamah.asi.cooler

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.HelpOutline
import androidx.compose.material.icons.automirrored.filled.MenuBook
import androidx.compose.material.icons.automirrored.filled.ShowChart
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.Thermostat
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Divider
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.NavigationDrawerItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import it.naamah.asi.cooler.ui.theme.CoolerTheme
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContent {
      CoolerTheme {
        CoolerApp()
      }
    }
  }
}

private enum class AppDestination(
    val route: String,
    val title: String,
    val icon: ImageVector,
) {
  Home("home", "Dashboard", Icons.Default.Thermostat),
  Debug("debug", "Debug", Icons.AutoMirrored.Filled.ShowChart),
  About("about", "About", Icons.Default.Info),
}

private data class StatusTile(
    val title: String,
    val value: String,
    val caption: String,
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun CoolerApp() {
  val navController = rememberNavController()
  val drawerState = rememberDrawerState(initialValue = DrawerValue.Closed)
  val scope = rememberCoroutineScope()
  val backStackEntry = navController.currentBackStackEntryAsState().value
  val currentDestination = backStackEntry?.destination
  val currentScreen = AppDestination.entries.firstOrNull { destination ->
    currentDestination?.hierarchy?.any { it.route == destination.route } == true
  } ?: AppDestination.Home

  ModalNavigationDrawer(
      drawerState = drawerState,
      drawerContent = {
        ModalDrawerSheet {
          Text(
              text = "ASI Smart Cooler",
              style = MaterialTheme.typography.titleLarge,
              modifier = Modifier.padding(horizontal = 24.dp, vertical = 20.dp))
          Divider(modifier = Modifier.padding(bottom = 8.dp))
          AppDestination.entries.forEach { destination ->
            NavigationDrawerItem(
                label = { Text(destination.title) },
                selected = destination == currentScreen,
                icon = { Icon(destination.icon, contentDescription = null) },
                onClick = {
                  scope.launch { drawerState.close() }
                  navController.navigate(destination.route) {
                    popUpTo(navController.graph.findStartDestination().id) { saveState = true }
                    launchSingleTop = true
                    restoreState = true
                  }
                },
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp))
          }
        }
      }) {
        Scaffold(
            topBar = {
              TopAppBar(
                  title = { Text(currentScreen.title) },
                  navigationIcon = {
                    androidx.compose.material3.IconButton(onClick = {
                      scope.launch { drawerState.open() }
                    }) {
                      Icon(Icons.Default.Menu, contentDescription = "Apri menu")
                    }
                  },
                  colors = TopAppBarDefaults.topAppBarColors(
                      containerColor = MaterialTheme.colorScheme.surface,
                  ))
            }) { innerPadding ->
              NavHost(
                  navController = navController,
                  startDestination = AppDestination.Home.route,
                  modifier = Modifier
                      .fillMaxSize()
                      .padding(innerPadding)) {
                    composable(AppDestination.Home.route) { HomeScreen() }
                    composable(AppDestination.Debug.route) { DebugScreen() }
                    composable(AppDestination.About.route) { AboutScreen() }
                  }
            }
      }
}

@Composable
private fun HomeScreen() {
  val tiles = listOf(
      StatusTile("Lato freddo", "--", "Temperatura NTC fredda"),
      StatusTile("Lato caldo", "--", "Temperatura NTC calda"),
      StatusTile("Ambiente", "--", "Temperatura aria ambiente"),
      StatusTile("Dew point", "--", "Margine anti-condensa"),
      StatusTile("Corrente", "--", "Assorbimento Peltier"),
      StatusTile("Duty PWM", "--", "Potenza applicata"),
      StatusTile("Target", "--", "Setpoint freddo"),
      StatusTile("Target effettivo", "--", "Target corretto da dew clamp"),
      StatusTile("Dew margin", "--", "Margine sopra il dew point"),
      StatusTile("Duty max", "--", "Limite massimo di uscita"),
      StatusTile("Protezione caldo", "--", "Stato protezione termica"),
      StatusTile("Sensore corrente", "--", "INA219 / ACS71x"),
  )

  LazyVerticalGrid(
      columns = GridCells.Adaptive(minSize = 160.dp),
      modifier = Modifier.fillMaxSize(),
      contentPadding = PaddingValues(16.dp),
      horizontalArrangement = Arrangement.spacedBy(12.dp),
      verticalArrangement = Arrangement.spacedBy(12.dp)) {
        items(tiles) { tile ->
          StatusCard(tile = tile)
        }
      }
}

@Composable
private fun StatusCard(tile: StatusTile) {
  Card(
      colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
      modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
          Text(tile.title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
          Text(tile.value, style = MaterialTheme.typography.headlineMedium)
          Text(tile.caption, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
      }
}

@Composable
private fun DebugScreen() {
  val rows = listOf(
      "Grafici trend" to "Spostati in una vista dedicata successiva",
      "Shunt INA219" to "--",
      "Potenza INA219" to "--",
      "Bus Peltier" to "--",
      "Raw NTC freddo" to "--",
      "Raw NTC caldo" to "--",
      "ADC corrente" to "--",
      "I2C scan" to "--",
      "PWM manuale" to "0 / 25 / 50 / 75 / 100",
  )

  Column(
      modifier = Modifier
          .fillMaxSize()
          .padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(
            text = "Debug e diagnostica avanzata",
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.SemiBold)
        rows.forEach { (label, value) ->
          Card {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically) {
                  Text(label, style = MaterialTheme.typography.bodyLarge)
                  Text(value, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.primary)
                }
          }
        }
      }
}

@Composable
private fun AboutScreen() {
  val items = listOf(
      Triple("Versione firmware", "--", Icons.Default.Info),
      Triple("Repository GitHub", "https://github.com/naamah75/asi-smart-cooler-diy", Icons.AutoMirrored.Filled.MenuBook),
      Triple("Grafici e debug", "Separati dalla dashboard principale", Icons.AutoMirrored.Filled.ShowChart),
      Triple("Supporto", "Config WiFi e API JSON del controller", Icons.AutoMirrored.Filled.HelpOutline),
  )

  Column(
      modifier = Modifier
          .fillMaxSize()
          .padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(
            text = "About",
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.SemiBold)
        items.forEach { (title, value, icon) ->
          Card {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.CenterVertically) {
                  Icon(icon, contentDescription = null)
                  Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(title, style = MaterialTheme.typography.titleMedium)
                    Text(value, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                  }
                }
          }
        }
      }
}
