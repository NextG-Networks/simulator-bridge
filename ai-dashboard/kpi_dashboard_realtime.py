#!/usr/bin/env python3
"""
KPI Visualization Dashboard - True Real-Time
Uses Streamlit with optimized auto-refresh for real-time updates without page reloads

Usage:
    pip install streamlit pandas plotly watchdog
    streamlit run kpi_dashboard_realtime.py
"""

import streamlit as st
import pandas as pd
import plotly.express as px
import plotly.graph_objects as go
from plotly.subplots import make_subplots
import os
import time
from datetime import datetime
from pathlib import Path
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
import threading

# Configuration
CSV_GNB_FILE = "../gnb_kpis.csv"
CSV_UE_FILE = "../ue_kpis.csv"
AUTO_REFRESH_INTERVAL = 1.0  # seconds

# Page config
st.set_page_config(
    page_title="NS3 KPI Dashboard - Real-Time",
    page_icon="📊",
    layout="wide",
    initial_sidebar_state="expanded"
)

# Initialize session state
if 'last_gnb_mtime' not in st.session_state:
    st.session_state.last_gnb_mtime = 0
if 'last_ue_mtime' not in st.session_state:
    st.session_state.last_ue_mtime = 0
if 'refresh_count' not in st.session_state:
    st.session_state.refresh_count = 0
if 'auto_refresh' not in st.session_state:
    st.session_state.auto_refresh = True

@st.cache_data(ttl=0.1, max_entries=10)  # Very short cache, keep only recent data
def load_csv_data():
    """Load CSV files and return dataframes with file modification time tracking"""
    gnb_df = pd.DataFrame()
    ue_df = pd.DataFrame()
    
    # Track file modification times
    gnb_mtime = 0
    ue_mtime = 0
    
    if os.path.exists(CSV_GNB_FILE):
        try:
            gnb_mtime = os.path.getmtime(CSV_GNB_FILE)
            # Only reload if file changed
            if gnb_mtime != st.session_state.last_gnb_mtime:
                gnb_df = pd.read_csv(CSV_GNB_FILE)
                if 'timestamp' in gnb_df.columns:
                    gnb_df['timestamp'] = pd.to_datetime(gnb_df['timestamp'], errors='coerce', unit='ms')
                st.session_state.last_gnb_mtime = gnb_mtime
        except Exception as e:
            st.error(f"Error loading {CSV_GNB_FILE}: {e}")
    
    if os.path.exists(CSV_UE_FILE):
        try:
            ue_mtime = os.path.getmtime(CSV_UE_FILE)
            # Only reload if file changed
            if ue_mtime != st.session_state.last_ue_mtime:
                ue_df = pd.read_csv(CSV_UE_FILE)
                if 'timestamp' in ue_df.columns:
                    ue_df['timestamp'] = pd.to_datetime(ue_df['timestamp'], errors='coerce', unit='ms')
                st.session_state.last_ue_mtime = ue_mtime
        except Exception as e:
            st.error(f"Error loading {CSV_UE_FILE}: {e}")
    
    return gnb_df, ue_df

def plot_time_series(df, y_column, title, color=None):
    """Create a time series plot"""
    if df.empty or y_column not in df.columns:
        return None
    
    # Filter out NaN values
    plot_df = df[['timestamp', y_column]].dropna()
    if plot_df.empty:
        return None
    
    fig = px.line(
        plot_df,
        x='timestamp',
        y=y_column,
        title=title,
        labels={'timestamp': 'Time', y_column: y_column.replace('_', ' ')},
        color=color if color and color in df.columns else None
    )
    fig.update_layout(
        hovermode='x unified',
        height=400,
        xaxis_title="Time",
        yaxis_title=y_column.replace('_', ' '),
        uirevision='constant'  # Preserve zoom/pan on updates
    )
    return fig

def plot_multi_metric(df, metrics, title, color_col=None):
    """Plot multiple metrics on the same chart"""
    if df.empty:
        return None
    
    fig = make_subplots(
        rows=len(metrics),
        cols=1,
        shared_xaxes=True,
        vertical_spacing=0.05,
        subplot_titles=metrics
    )
    
    for i, metric in enumerate(metrics):
        if metric in df.columns:
            plot_df = df[['timestamp', metric]].dropna()
            if not plot_df.empty:
                fig.add_trace(
                    go.Scatter(
                        x=plot_df['timestamp'],
                        y=plot_df[metric],
                        name=metric,
                        mode='lines+markers',
                        line=dict(width=2)
                    ),
                    row=i+1, col=1
                )
    
    fig.update_layout(
        title=title,
        height=300 * len(metrics),
        showlegend=True,
        hovermode='x unified',
        uirevision='constant'  # Preserve zoom/pan on updates
    )
    
    return fig

def main():
    st.title("📊 NS3 KPI Dashboard - Real-Time")
    st.markdown("**Live** visualization of network KPIs from NS3 simulation")
    
    # Increment refresh count
    st.session_state.refresh_count += 1
    
    # Load data
    gnb_df, ue_df = load_csv_data()
    
    # Real-time indicator
    col1, col2, col3 = st.columns([1, 2, 1])
    with col2:
        current_time = datetime.now().strftime('%H:%M:%S')
        st.markdown(f"<div style='text-align: center; padding: 10px; background-color: #e8f5e9; border-radius: 5px;'>"
                   f"🟢 Live | Refresh #{st.session_state.refresh_count} | {current_time}"
                   f"</div>", unsafe_allow_html=True)
    
    # Sidebar controls
    with st.sidebar:
        st.header("⚙️ Real-Time Controls")
        
        auto_refresh = st.checkbox("🔄 Real-Time Auto-Refresh", value=st.session_state.auto_refresh, key="auto_refresh_checkbox")
        st.session_state.auto_refresh = auto_refresh
        
        if auto_refresh:
            refresh_interval = st.slider("Refresh interval (seconds)", 0.5, 5.0, AUTO_REFRESH_INTERVAL, 0.5, key="refresh_slider")
            st.caption(f"Updating every {refresh_interval}s")
        else:
            refresh_interval = AUTO_REFRESH_INTERVAL
        
        st.markdown("---")
        st.header("📊 Live Status")
        
        # Show last update time
        if 'last_update' not in st.session_state:
            st.session_state.last_update = datetime.now()
        
        time_since_update = (datetime.now() - st.session_state.last_update).total_seconds()
        st.metric("Last Update", f"{time_since_update:.1f}s ago")
        
        # Show data freshness
        if not gnb_df.empty and 'timestamp' in gnb_df.columns:
            latest_time = gnb_df['timestamp'].max()
            if pd.notna(latest_time):
                if isinstance(latest_time, pd.Timestamp):
                    time_diff = (datetime.now() - latest_time.to_pydatetime()).total_seconds()
                else:
                    time_diff = (datetime.now() - latest_time).total_seconds()
                st.metric("Latest Data", f"{time_diff:.1f}s ago")
        
        st.session_state.last_update = datetime.now()
        
        st.markdown("---")
        st.header("📁 Data Files")
        gnb_exists = os.path.exists(CSV_GNB_FILE)
        ue_exists = os.path.exists(CSV_UE_FILE)
        
        st.write(f"**gNB KPIs:** {'✅' if gnb_exists else '❌'} {CSV_GNB_FILE}")
        st.write(f"**UE KPIs:** {'✅' if ue_exists else '❌'} {CSV_UE_FILE}")
        
        if st.button("🔄 Refresh Now"):
            st.cache_data.clear()
            st.rerun()
        
        st.markdown("---")
        st.markdown("### 📊 Available Metrics")
        st.caption("Select metrics to display in the main view")
    
    # Main content tabs
    tab1, tab2, tab3, tab4 = st.tabs(["📡 Cell-Level (gNB)", "📱 UE-Level", "📈 Combined View", "📋 Data Tables"])
    
    # Tab 1: Cell-Level KPIs
    with tab1:
        st.header("Cell-Level KPIs (gNB)")
        
        if gnb_df.empty:
            st.warning(f"No data found in {CSV_GNB_FILE}. Make sure the simulation is running and generating data.")
        else:
            col1, col2, col3 = st.columns(3)
            with col1:
                st.metric("Total Records", len(gnb_df))
            with col2:
                if 'timestamp' in gnb_df.columns:
                    latest_time = gnb_df['timestamp'].max()
                    if pd.notna(latest_time):
                        if isinstance(latest_time, pd.Timestamp):
                            st.metric("Latest Data", latest_time.strftime('%H:%M:%S'))
                        else:
                            st.metric("Latest Data", str(latest_time))
            with col3:
                if len(gnb_df) > 1 and 'timestamp' in gnb_df.columns:
                    time_span = (gnb_df['timestamp'].max() - gnb_df['timestamp'].min()).total_seconds()
                    st.metric("Time Span", f"{time_span:.0f}s")
            
            # Get numeric columns
            numeric_cols = gnb_df.select_dtypes(include=['float64', 'int64']).columns.tolist()
            exclude_cols = ['cell_id']
            plot_cols = [col for col in numeric_cols if col not in exclude_cols]
            
            if plot_cols:
                selected_metrics = st.multiselect(
                    "Select metrics to display",
                    plot_cols,
                    default=plot_cols[:min(4, len(plot_cols))] if len(plot_cols) > 0 else [],
                    key="gnb_metrics"
                )
                
                if selected_metrics:
                    # Group by cell_id if available
                    if 'cell_id' in gnb_df.columns:
                        cell_ids = gnb_df['cell_id'].dropna().unique()
                        if len(cell_ids) > 1:
                            cell_ids_list = [str(cid) for cid in cell_ids]
                            selected_cells = st.multiselect(
                                "Filter by Cell ID",
                                cell_ids_list,
                                default=cell_ids_list,
                                key="gnb_cells"
                            )
                            # Convert selected_cells back to match original cell_id types for filtering
                            gnb_df_filtered = gnb_df[gnb_df['cell_id'].astype(str).isin(selected_cells)]
                        else:
                            gnb_df_filtered = gnb_df
                    else:
                        gnb_df_filtered = gnb_df
                    
                    # Plot selected metrics
                    for metric in selected_metrics:
                        if metric in gnb_df_filtered.columns:
                            fig = plot_time_series(gnb_df_filtered, metric, f"{metric.replace('_', ' ').title()}")
                            if fig:
                                st.plotly_chart(fig, width='stretch')
                    
                    # Multi-metric view
                    if len(selected_metrics) > 1:
                        st.subheader("Combined View")
                        fig = plot_multi_metric(gnb_df_filtered, selected_metrics, "Cell-Level KPIs Over Time")
                        if fig:
                            st.plotly_chart(fig, width='stretch')
            else:
                st.info("No numeric metrics found in gNB data")
    
    # Tab 2: UE-Level KPIs
    with tab2:
        st.header("UE-Level KPIs")
        
        if ue_df.empty:
            st.warning(f"No data found in {CSV_UE_FILE}. Make sure the simulation is running and generating data.")
        else:
            col1, col2, col3 = st.columns(3)
            with col1:
                st.metric("Total Records", len(ue_df))
            with col2:
                if 'timestamp' in ue_df.columns:
                    latest_time = ue_df['timestamp'].max()
                    if pd.notna(latest_time):
                        if isinstance(latest_time, pd.Timestamp):
                            st.metric("Latest Data", latest_time.strftime('%H:%M:%S'))
                        else:
                            st.metric("Latest Data", str(latest_time))
            with col3:
                unique_ues = ue_df['ue_id'].nunique() if 'ue_id' in ue_df.columns else 0
                st.metric("Unique UEs", unique_ues)
            
            # Get numeric columns
            numeric_cols = ue_df.select_dtypes(include=['float64', 'int64']).columns.tolist()
            exclude_cols = ['ue_id', 'cell_id']
            plot_cols = [col for col in numeric_cols if col not in exclude_cols]
            
            if plot_cols:
                selected_metrics = st.multiselect(
                    "Select metrics to display",
                    plot_cols,
                    default=plot_cols[:min(4, len(plot_cols))] if len(plot_cols) > 0 else [],
                    key="ue_metrics"
                )
                
                if selected_metrics:
                    # Filter by UE
                    if 'ue_id' in ue_df.columns:
                        ue_ids = ue_df['ue_id'].dropna().unique()
                        ue_ids_list = [str(uid) for uid in ue_ids]
                        selected_ues = st.multiselect(
                            "Filter by UE ID",
                            ue_ids_list,
                            default=ue_ids_list[:min(5, len(ue_ids_list))],
                            key="ue_filter"
                        )
                        # Convert selected_ues back to match original ue_id types for filtering
                        ue_df_filtered = ue_df[ue_df['ue_id'].astype(str).isin(selected_ues)]
                    else:
                        ue_df_filtered = ue_df
                    
                    # Filter by cell
                    if 'cell_id' in ue_df_filtered.columns:
                        cell_ids = ue_df_filtered['cell_id'].dropna().unique()
                        if len(cell_ids) > 1:
                            cell_ids_list = [str(cid) for cid in cell_ids]
                            selected_cells = st.multiselect(
                                "Filter by Cell ID",
                                cell_ids_list,
                                default=cell_ids_list,
                                key="ue_cells"
                            )
                            # Convert selected_cells back to match original cell_id types for filtering
                            ue_df_filtered = ue_df_filtered[ue_df_filtered['cell_id'].astype(str).isin(selected_cells)]
                    
                    # Plot selected metrics
                    for metric in selected_metrics:
                        if metric in ue_df_filtered.columns:
                            color_col = 'ue_id' if 'ue_id' in ue_df_filtered.columns else None
                            fig = plot_time_series(ue_df_filtered, metric, f"{metric.replace('_', ' ').title()}", color=color_col)
                            if fig:
                                st.plotly_chart(fig, width='stretch')
                    
                    # Summary statistics
                    st.subheader("Summary Statistics")
                    st.dataframe(ue_df_filtered[selected_metrics].describe(), width='stretch')
            else:
                st.info("No numeric metrics found in UE data")
    
    # Tab 3: Combined View
    with tab3:
        st.header("Combined Analysis")
        
        if gnb_df.empty and ue_df.empty:
            st.warning("No data available for combined view")
        else:
            col1, col2 = st.columns(2)
            
            with col1:
                st.subheader("Cell-Level Overview")
                if not gnb_df.empty and 'timestamp' in gnb_df.columns:
                    latest_gnb = gnb_df.iloc[-1] if len(gnb_df) > 0 else None
                    if latest_gnb is not None:
                        st.write("**Latest Values:**")
                        for col in gnb_df.select_dtypes(include=['float64', 'int64']).columns:
                            if col != 'cell_id':
                                st.metric(col.replace('_', ' ').title(), f"{latest_gnb[col]:.2f}" if pd.notna(latest_gnb[col]) else "N/A")
            
            with col2:
                st.subheader("UE-Level Overview")
                if not ue_df.empty and 'timestamp' in ue_df.columns:
                    latest_ue = ue_df.groupby('ue_id').last() if 'ue_id' in ue_df.columns else ue_df.iloc[-1:]
                    if not latest_ue.empty:
                        st.write("**Latest Values (per UE):**")
                        if isinstance(latest_ue, pd.DataFrame) and len(latest_ue) > 0:
                            display_cols = [col for col in ue_df.select_dtypes(include=['float64', 'int64']).columns if col not in ['ue_id', 'cell_id']]
                            st.dataframe(latest_ue[display_cols].head(10), width='stretch')
            
            # Correlation analysis
            if not ue_df.empty:
                st.subheader("Metric Correlations")
                numeric_cols = ue_df.select_dtypes(include=['float64', 'int64']).columns.tolist()
                correlation_cols = [col for col in numeric_cols if col not in ['ue_id', 'cell_id']]
                if len(correlation_cols) > 1:
                    corr_matrix = ue_df[correlation_cols].corr()
                    fig = px.imshow(
                        corr_matrix,
                        title="Correlation Matrix",
                        color_continuous_scale='RdBu',
                        aspect="auto"
                    )
                    st.plotly_chart(fig, width='stretch')
    
    # Tab 4: Data Tables
    with tab4:
        st.header("Raw Data")
        
        col1, col2 = st.columns(2)
        
        with col1:
            st.subheader("gNB Data")
            if not gnb_df.empty:
                st.dataframe(gnb_df, width='stretch', height=400)
                st.download_button(
                    label="📥 Download gNB CSV",
                    data=gnb_df.to_csv(index=False),
                    file_name=f"gnb_kpis_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv",
                    mime="text/csv",
                    key="gnb_download"
                )
            else:
                st.info("No gNB data available")
        
        with col2:
            st.subheader("UE Data")
            if not ue_df.empty:
                st.dataframe(ue_df, width='stretch', height=400)
                st.download_button(
                    label="📥 Download UE CSV",
                    data=ue_df.to_csv(index=False),
                    file_name=f"ue_kpis_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv",
                    mime="text/csv",
                    key="ue_download"
                )
            else:
                st.info("No UE data available")
    
    # Real-time auto-refresh using st.rerun()
    if auto_refresh:
        time.sleep(refresh_interval)
        st.rerun()

if __name__ == "__main__":
    main()

